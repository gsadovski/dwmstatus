/* My dwmstatus bar

  Song played in Artist - Name format
  Volume
  Disk usage - home and root
  Network monitoring - should display SSID in green when connected or
                       'down' in red when not connected. Preferably would
		       also display signal strength with color dependant on
		       the value. Finally it should also display download and
		       upload speeds.
  Memory usage - should not include cache. Should be for both swap and main.
  - CPU usage - Load average followed by average CPU load
  - CPU temperatures - Coloured red above a threshold
  - Battery indicator - color dependadnt with icon depending on state.
  - Date and time
*/

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <alsa/asoundlib.h>
#include <alsa/control.h>

#include <X11/Xlib.h>

static Display *dpy;

/* C string utility */

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);
	
	return ret;
}

/* Volume info */
/* Inexplicably much heavier on resources than the other functions */

#define GLYPH_VOL_MUTE ""
#define GLYPH_VOL_LOW  ""
#define GLYPH_VOL_HIGH ""

char * 
getvol(void)
{ 
  long int vol, max, min;
  int mute_state;
  int pct_vol;
  snd_mixer_t *handle;
  snd_mixer_elem_t *elem;
  snd_mixer_selem_id_t *s_elem;
  char *s;
    
  snd_mixer_open(&handle, 0);
  snd_mixer_attach(handle, "default");
  snd_mixer_selem_register(handle, NULL, NULL);
  snd_mixer_load(handle);
  snd_mixer_selem_id_malloc(&s_elem);
  snd_mixer_selem_id_set_name(s_elem, "Master");
  elem = snd_mixer_find_selem(handle, s_elem);
  
  if (elem == NULL)
    {
      snd_mixer_selem_id_free(s_elem);
      snd_mixer_close(handle);
      perror("alsa error");
      return smprintf("");
    }
  
  snd_mixer_handle_events(handle);
  snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
  snd_mixer_selem_get_playback_volume(elem, 0, &vol);
  snd_mixer_selem_get_playback_switch(elem, 0, &mute_state);
  
  if(!mute_state)
    return smprintf("%s MUTE", GLYPH_VOL_MUTE);

  pct_vol = (int)((float)(vol * 100) / max + 0.5);

  if (pct_vol < 50)
    s = GLYPH_VOL_LOW;
  else
    s = GLYPH_VOL_HIGH;
  
  snd_mixer_selem_id_free(s_elem);
  snd_mixer_close(handle);
  
  return smprintf("%s%3d%%", s, pct_vol);
}

/* Time info */

char *
mktimes(char *fmt)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}

char *
mktimestz(char *fmt, char *tzname)
{
  	setenv("TZ", tzname, 1);
	char *buf = mktimes(fmt);
	unsetenv("TZ");

	return buf;
}

/* CPU info */

#define STAT_FILE "/proc/stat"
#define TICS_EDGE  20

int num_cpus;

typedef struct CT_t
{
  unsigned long long u, n, s, i, w, x, y, z;
  unsigned long long tot;
  unsigned long long edge;
} CT_t;

void
getnumcpus(void)
{
  num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (num_cpus<1)        /* SPARC glibc is buggy */
    num_cpus=1;
}

char *
getcpuload(void)
{
  static FILE *fp = NULL;
  static CT_t tics_prv;
  static CT_t tics_cur;
  static CT_t tics_frme;
  float scale;
  double avgs[3];
  float pct_tot;

  if (getloadavg(avgs, 3) < 0)
    {
      perror("cpuload call to getloadavg failed");
      return smprintf("");
    }

  if ((fp = fopen(STAT_FILE, "r")))
    {
      memcpy(&tics_prv, &tics_cur, sizeof(CT_t));
      fscanf(fp, "cpu %llu %llu %llu %llu %llu %llu %llu %llu"
	     , &tics_cur.u, &tics_cur.n, &tics_cur.s
	     , &tics_cur.i, &tics_cur.w, &tics_cur.x
	     , &tics_cur.y, &tics_cur.z);
      fclose(fp);
    }
  else
    {
      perror("cpuload failed to read CPU tic values");
      return smprintf("");
    }

  tics_cur.tot = tics_cur.u + tics_cur.s
    + tics_cur.n + tics_cur.i + tics_cur.w
    + tics_cur.x + tics_cur.y + tics_cur.z;
  tics_cur.edge =
    ((tics_cur.tot - tics_prv.tot) / num_cpus) / (100 / TICS_EDGE);

  tics_frme.u = tics_cur.u - tics_prv.u;
  tics_frme.s = tics_cur.s - tics_prv.s;
  tics_frme.n = tics_cur.n - tics_prv.n;
  tics_frme.i = tics_cur.i - tics_prv.i;
  tics_frme.w = tics_cur.w - tics_prv.w;
  tics_frme.x = tics_cur.x - tics_prv.x;
  tics_frme.y = tics_cur.y - tics_prv.y;
  tics_frme.z = tics_cur.z - tics_prv.z;
  tics_frme.tot = tics_frme.u + tics_frme.s + tics_frme.n + tics_frme.i +
    tics_frme.w + tics_frme.x + tics_frme.y + tics_frme.z;

  if (tics_frme.tot < tics_cur.edge)
    {
      tics_frme.u = tics_frme.s = tics_frme.n = tics_frme.i =
  	tics_frme.w = tics_frme.x  = tics_frme.y = tics_frme.z = 0;
    }
  if (1 > tics_frme.tot)
    {
      tics_frme.i = tics_frme.tot = 1;
    }

  scale = 100.0 / (float)tics_frme.tot;

  pct_tot = (float)(tics_frme.u + tics_frme.n + tics_frme.s) * scale;

  return smprintf(" %.2f %.2f %.2f %6.2f%%",
		  avgs[0], avgs[1], avgs[2], pct_tot);
}

/*  Temperature info*/

#define TEMP_INPUT "/sys/class/hwmon/hwmon0/temp1_input"
#define TEMP_CRIT  "/sys/class/hwmon/hwmon0/temp1_crit"

char *
gettemperature(void)
{
  long temp, tempc;
  FILE *fp = NULL;
  
  if ((fp = fopen(TEMP_INPUT, "r")))
    {
      fscanf(fp, "%ld\n", &temp);
      fclose(fp);
      fp = fopen(TEMP_CRIT, "r");
      fscanf(fp, "%ld\n", &tempc);
      fclose(fp);
    }
  else
    {
      perror("failed to open temperature file");
      return smprintf("");
    }

  return smprintf(" %3ld°C", temp / 1000);
}

/* Battery info */

#define BATT_NOW        "/sys/class/power_supply/BAT0/energy_now"
#define BATT_FULL       "/sys/class/power_supply/BAT0/energy_full"
#define BATT_STATUS     "/sys/class/power_supply/BAT0/status"
#define POW_NOW         "/sys/class/power_supply/BAT0/power_now"

#define GLYPH_UNKWN   "? "
#define GLYPH_FULL    " "
#define GLYPH_CHRG    " "
#define GLYPH_DCHRG_0 " "
#define GLYPH_DCHRG_1 " "
#define GLYPH_DCHRG_2 " "
#define GLYPH_DCHRG_3 " "
#define GLYPH_DCHRG_4 " "

char *
getbattery()
{
  long lnum1, lnum2 = 0;
  long pow, pct, energy = -1;
  long mm, hh;
  char *status = malloc(sizeof(char)*12);
  char *s = GLYPH_UNKWN;
  FILE *fp = NULL;
  if ((fp = fopen(BATT_NOW, "r"))) {
    fscanf(fp, "%ld\n", &lnum1);
    fclose(fp);
    fp = fopen(BATT_FULL, "r");
    fscanf(fp, "%ld\n", &lnum2);
    fclose(fp);
    fp = fopen(BATT_STATUS, "r");
    fscanf(fp, "%s\n", status);
    fclose(fp);
    fp = fopen(POW_NOW, "r");
    fscanf(fp, "%ld\n", &pow);
    fclose(fp);
    pct = (lnum1/(lnum2/100));
    if (strcmp(status,"Charging") == 0)
      {
	s = GLYPH_CHRG;

	energy = lnum2 - lnum1;
      }
    else if (strcmp(status,"Discharging") == 0)
      {
	if (pct < 20)
	  s = GLYPH_DCHRG_0;
	else if (pct < 40)
	  s = GLYPH_DCHRG_1;
	else if (pct < 60)
	  s = GLYPH_DCHRG_2;
	else if (pct < 85)
	  s = GLYPH_DCHRG_3;
	else
	  s = GLYPH_DCHRG_4;

	energy = lnum1;
      }
    else if (strcmp(status,"Full") == 0)
      s = GLYPH_FULL;

    if ( energy < 0 || pow <= 0)
      return smprintf("%s%3ld%%", s,pct);
    else
      {
	hh = energy / pow;
	mm = (energy % pow) * 60 / pow;
	return smprintf("%s%3ld%% %2ld:%02ld", s,pct,hh,mm);
      }
  }
  else
    {
      perror("getbattery");
      return smprintf("");
    }
}

/* Main function */

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

int
main(void)
{
	char *status;
	char *cpu;
	char *temp;
	char *batt;
	char *tmloc;

	int counter = 0;
	int cpu_interval   = 2;
	int temp_interval  = 30;
	int batt_interval  = 30;
	int tmloc_interval = 1;
	int max_interval   = 30;

	/* Run functions that have to be run only once */
	getnumcpus();

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	/* Regular updates */
	for (;;sleep(1)) {
	        if (counter % cpu_interval == 0)   cpu  = getcpuload();
		if (counter % temp_interval == 0)  temp = gettemperature();
	        if (counter % batt_interval == 0)  batt = getbattery();
		if (counter % tmloc_interval == 0)
		  tmloc = mktimes("%Y-%m-%d %H:%M:%S %Z");

		status = smprintf("%s | %s | %s | %s",
				  cpu, temp, batt, tmloc);
		setstatus(status);

		if (!cpu)   free(cpu);
		if (!temp)  free(temp);
		if (!batt)  free(batt);
		if (!tmloc) free(tmloc);
		free(status);

		counter = (counter + 1) % max_interval;
	}

	XCloseDisplay(dpy);

	return 0;
}

