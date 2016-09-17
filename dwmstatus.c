/* My dwmstatus bar

   - Network monitoring - colors + should just display "Network down" in red if not connected
   - Memory usage - colors
   - CPU usage - colors
   - CPU temperatures - colors
   - Battery indicator - colors
   - Date and time - Automatically detect time zone
*/

#define _DEFAULT_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <err.h>
#include <errno.h>
#include <time.h>
#include <ifaddrs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <alsa/asoundlib.h>
#include <alsa/control.h>
#include <linux/wireless.h>

#include <X11/Xlib.h>

static Display *dpy;

/* C string utilities */

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
  if (ret == NULL)
    {
      warn("malloc failed in smprintf");
      exit(1);
    }

  va_start(fmtargs, fmt);
  vsnprintf(ret, len, fmt, fmtargs);
  va_end(fmtargs);
	
  return ret;
}

int
readvaluesfromfile(char *fn, char *fmt, ...)
{
  va_list fmtargs;
  FILE* fp = NULL;
  int rval = 1;
  
  if ((fp = fopen(fn, "r")))
    {
      va_start(fmtargs, fmt);
      vfscanf(fp, fmt, fmtargs);
      va_end(fmtargs);
      fclose(fp);
      rval = 0;
    }
  else
    {
      warn("Error opening %s", fn);
    }

  return rval;
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
      warn("alsa error");
      return smprintf("");
    }
  
  snd_mixer_handle_events(handle);
  snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
  snd_mixer_selem_get_playback_volume(elem, 0, &vol);
  snd_mixer_selem_get_playback_switch(elem, 0, &mute_state);
  
  if(!mute_state)
    {
      return smprintf("%s MUTE", GLYPH_VOL_MUTE);
    }

  pct_vol = (int)((float)(vol * 100) / max + 0.5);

  if (pct_vol < 50)
    {
      s = GLYPH_VOL_LOW;
    }
  else
    {
      s = GLYPH_VOL_HIGH;
    }
  
  snd_mixer_selem_id_free(s_elem);
  snd_mixer_close(handle);
  
  return smprintf("%s%3d%%", s, pct_vol);
}

/* Memory info */

#define MEMINFO_FILE "/proc/meminfo"

typedef struct mem_table_struct
{
  const char *name;     /* memory type name */
  unsigned long *slot; /* slot in return struct */
} mem_table_struct;

int
compare_mem_table_structs(const void *a, const void *b)
{
  return strcmp(((const mem_table_struct*)a)->name,((const mem_table_struct*)b)->name);
}

#define BAD_OPEN_MESSAGE					\
"Error: /proc must be mounted\n"				\
"  To mount /proc at boot you need an /etc/fstab line like:\n"	\
"      proc   /proc   proc    defaults\n"			\
"  In the meantime, run \"mount proc /proc -t proc\"\n"

#define FILE_TO_BUF(filename, fd) do{				\
    static int local_n;						\
    if (fd == -1 && (fd = open(filename, O_RDONLY)) == -1) {	\
	fputs(BAD_OPEN_MESSAGE, stderr);			\
	fflush(NULL);						\
	_exit(102);						\
    }								\
    lseek(fd, 0L, SEEK_SET);					\
    if ((local_n = read(fd, buf, sizeof buf - 1)) < 0) {	\
	perror(filename);					\
	fflush(NULL);						\
	_exit(103);						\
    }								\
    buf[local_n] = '\0';					\
}while(0)

char *
getmeminfo(void)
{
  static int meminfo_fd = -1;
  static char buf[8192];
  char namebuf[32]; /* big enough to hold any row name */
  mem_table_struct findme = { namebuf, NULL };
  mem_table_struct *found;
  char *head;
  char *tail;
  static unsigned long kb_main_buffers;
  static unsigned long kb_page_cache;
  static unsigned long kb_main_free;
  static unsigned long kb_main_total;
  static unsigned long kb_slab_reclaimable;
  static unsigned long kb_main_shared;
  static unsigned long kb_swap_cached;
  static unsigned long kb_swap_free;
  static unsigned long kb_swap_total;
  static const mem_table_struct mem_table[] =
    {
      {"Buffers",      &kb_main_buffers},
      {"Cached",       &kb_page_cache},
      {"MemFree",      &kb_main_free},
      {"MemTotal",     &kb_main_total},
      {"SReclaimable", &kb_slab_reclaimable},
      {"Shmem",        &kb_main_shared},
      {"SwapCached",   &kb_swap_cached},
      {"SwapFree",     &kb_swap_free},
      {"SwapTotal",    &kb_swap_total},
    };
  const int mem_table_count = sizeof(mem_table)/sizeof(mem_table_struct);
  unsigned long kb_main_cached, kb_swap_used, kb_main_used;
  float gb_main_used, gb_main_total, gb_swap_used, gb_swap_total;

  FILE_TO_BUF(MEMINFO_FILE,meminfo_fd);

  head = buf;
  for(;;)
    {
      tail = strchr(head, ':');
      if(!tail) break;
      *tail = '\0';
      if(strlen(head) >= sizeof(namebuf))
	{
	  head = tail+1;
	  goto nextline;
	}
      strcpy(namebuf,head);
      found = bsearch(&findme, mem_table, mem_table_count,
		      sizeof(mem_table_struct), compare_mem_table_structs
		      );
      head = tail+1;
      if(!found) goto nextline;
      *(found->slot) = (unsigned long)strtoull(head,&tail,10);
    nextline:
      tail = strchr(head, '\n');
      if(!tail) break;
      head = tail+1;
    }

  kb_main_cached = kb_page_cache + kb_slab_reclaimable;

  kb_swap_used = kb_swap_total - kb_swap_free - kb_swap_cached;
  kb_main_used = kb_main_total - kb_main_free - kb_main_cached - kb_main_buffers;

  gb_main_used  = (float)kb_main_used  / 1024 / 1024;
  gb_main_total = (float)kb_main_total / 1024 / 1024;
  gb_swap_used  = (float)kb_swap_used  / 1024 / 1024;
  gb_swap_total = (float)kb_swap_total / 1024 / 1024;
  
  return smprintf(" %4.1f/%4.1f GiB  %4.2f/%4.2f GiB",
		  gb_main_used, gb_main_total,
		  gb_swap_used, gb_swap_total);
}

/* Disk info */

char *
diskfree(const char *mountpoint)
{
  struct statvfs fs;
  float freespace;
  
  if (statvfs(mountpoint, &fs) < 0)
    {
      warn("Could not get %s filesystem info", mountpoint);
      return smprintf("");
    }

  freespace = (float)fs.f_bsize * (float)fs.f_bfree / 1024 / 1024 / 1024;
  if (freespace < 100)
    return smprintf("%4.1f GiB", freespace);
  else
    return smprintf("%.0f GiB", freespace);
}

char *
getdiskusage(void)
{
  char *root = diskfree("/");
  char *home = diskfree("/home");
  char *s = smprintf(" %s  %s", root, home);

  free(root);
  free(home);
  return s;
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
  if (timtm == NULL)
    {
      warn("Error when calling localtime");
      return smprintf("");
    }

  if (!strftime(buf, sizeof(buf)-1, fmt, timtm))
    {
      warn("strftime == 0");
      return smprintf("");
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

/* Network info */

#define WIFICARD             "wlp3s0"
#define WIREDCARD            "eth0"
#define NETDEV_FILE          "/proc/net/dev"
#define WIFI_OPERSTATE_FILE  "/sys/class/net/"WIFICARD"/operstate"
#define WIRED_OPERSTATE_FILE "/sys/class/net/"WIREDCARD"/operstate"
#define WIRELESS_FILE        "/proc/net/wireless"

int
parsenetdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
  char buf[255];
  char *datastart;
  static int bufsize;
  int rval;
  FILE *fp = NULL;
  unsigned long long int receivedacc, sentacc;

  bufsize = 255;
  rval = 1;

  if ((fp = fopen(NETDEV_FILE, "r")))
    {
      // Ignore the first two lines of the file
      fgets(buf, bufsize, fp);
      fgets(buf, bufsize, fp);
  
      while (fgets(buf, bufsize, fp))
	{
	  if ((datastart = strstr(buf, "lo:")) == NULL)
	    {
	      datastart = strstr(buf, ":");
	  
	      // With thanks to the conky project at http://conky.sourceforge.net/
	      sscanf(datastart + 1,
		     "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",
		     &receivedacc, &sentacc);

	      *receivedabs += receivedacc;
	      *sentabs += sentacc;
	      rval = 0;
	    }
	}

      fclose(fp);
    }
  else
    {
      warn("Error opening %s", NETDEV_FILE);
    }

  return rval;
}

char *
calculatespeed(unsigned long long int newval, unsigned long long int oldval)
{
  double speed;
  speed = (newval - oldval) / 1024.0;
  if (speed > 1024.0)
    {
      speed /= 1024.0;
      return smprintf("%4.1f MiB/s", speed);
    }
  else
    {
      return smprintf("%4.0f KiB/s", speed);
    }
}

char *
getbandwidth(void)
{
  static unsigned long long int rec, sent;
  unsigned long long int newrec, newsent;
  newrec = newsent = 0;
  char *downstr, *upstr;
  char *bandwidth;

  if (parsenetdev(&newrec, &newsent)) return smprintf("");

  downstr = calculatespeed(newrec, rec);
  upstr = calculatespeed(newsent, sent);

  rec = newrec;
  sent = newsent;
  bandwidth = smprintf("▼ %s ▲ %s", downstr, upstr);

  free(downstr);
  free(upstr);
  
  return bandwidth;
}

char *
getwifistrength()
{
  int strength;
  char buf[255];
  char *datastart;
  FILE *fp = NULL;

  if ((fp = fopen(WIRELESS_FILE, "r")))
    {
      fgets(buf, sizeof(buf), fp);
      fgets(buf, sizeof(buf), fp);
      fgets(buf, sizeof(buf), fp);
      fclose(fp);
    }
  else
    {
      warn("Error opening %s", WIRELESS_FILE);
      return smprintf("");
    }

  datastart = strstr(buf, WIFICARD":");
  if (datastart != NULL)
    {
      datastart = strstr(buf, ":");
      sscanf(datastart + 1, " %*d   %d  %*d  %*d		  %*d	   %*d		%*d		 %*d	  %*d		 %*d", &strength);
    }

  return smprintf("%d%%", strength);
}

char *
getwifiessid()
{
  char id[IW_ESSID_MAX_SIZE+1];
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct iwreq wreq;

  memset(&wreq, 0, sizeof(struct iwreq));
  wreq.u.essid.length = IW_ESSID_MAX_SIZE+1;
  sprintf(wreq.ifr_name, WIFICARD);
  if (sockfd == -1)
    {
      warn("Cannot open socket for interface: %s", WIFICARD);
      return smprintf("");
    }
  wreq.u.essid.pointer = id;
  if (ioctl(sockfd,SIOCGIWESSID, &wreq) == -1)
    {
      warn("Get ESSID ioctl failed for interface %s", WIFICARD);
      return smprintf("");
    }
  
  close(sockfd);
  if (strcmp((char *)wreq.u.essid.pointer, "") == 0)
    {
      return smprintf("");
    }
  else
    {
      return smprintf("%s", (char *)wreq.u.essid.pointer);
    }
}

char *
getconnection(void)
{
  char status[5];
  char *strength;
  char *essid;
  char *connection;

  if (readvaluesfromfile(WIFI_OPERSTATE_FILE, "%s\n", status)) return smprintf("");
  if(strcmp(status, "up") == 0)
    {
      strength = getwifistrength();
      essid = getwifiessid();
      connection = smprintf("%s %s", essid, strength);
      free(strength);
      free(essid);
      return connection;
    }

  if (readvaluesfromfile(WIRED_OPERSTATE_FILE, "%s\n", status)) return smprintf("");
  if (strcmp(status, "up") == 0)
    {
      connection = smprintf("Wired");
      return connection;
    }

  return smprintf("");
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
  static CT_t tics_prv;
  static CT_t tics_cur;
  static CT_t tics_frme;
  float scale;
  double avgs[3];
  float pct_tot;

  if (getloadavg(avgs, 3) < 0)
    {
      warn("cpuload call to getloadavg failed");
      return smprintf("");
    }

  memcpy(&tics_prv, &tics_cur, sizeof(CT_t));

  if (readvaluesfromfile(STAT_FILE, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
			 &tics_cur.u, &tics_cur.n, &tics_cur.s, &tics_cur.i,
			 &tics_cur.w, &tics_cur.x, &tics_cur.y, &tics_cur.z))
      return smprintf("");
  
  tics_cur.tot = tics_cur.u + tics_cur.s + tics_cur.n + tics_cur.i +
      tics_cur.w + tics_cur.x + tics_cur.y + tics_cur.z;
  tics_cur.edge = ((tics_cur.tot - tics_prv.tot) / num_cpus) / (100 / TICS_EDGE);

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

  if (readvaluesfromfile(TEMP_INPUT, "%ld\n", &temp)) return smprintf("");
  if (readvaluesfromfile(TEMP_CRIT, "%ld\n", &tempc)) return smprintf("");

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
  long battnow, battfull = 0;
  long pow, pct, energy = -1;
  long mm, hh;
  char status[12];
  char *s = GLYPH_UNKWN;

  if (readvaluesfromfile(BATT_NOW, "%ld\n", &battnow)) return smprintf("");
  if (readvaluesfromfile(BATT_FULL, "%ld\n", &battfull)) return smprintf("");
  if (readvaluesfromfile(BATT_STATUS, "%s\n", status)) return smprintf("");
  if (readvaluesfromfile(POW_NOW, "%ld\n", &pow)) return smprintf("");
  
  pct = 100*battnow/battfull;

  if (strcmp(status,"Charging") == 0)
    {
      s = GLYPH_CHRG;
      
      energy = battfull - battnow;
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
      
      energy = battnow;
    }
  else if (strcmp(status,"Full") == 0)
    s = GLYPH_FULL;

  if ( energy < 0 || pow <= 0)
    {
      return smprintf("%s%3ld%%", s,pct);
    }
  else
    {
      hh = energy / pow;
      mm = (energy % pow) * 60 / pow;
      return smprintf("%s%3ld%% %2ld:%02ld", s,pct,hh,mm);
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
  char *conn;
  char *bw;
  char *cpu;
  char *mem;
  char *disk;
  char *temp;
  char *batt;
  char *tmloc;

  int counter = 0;
  int conn_interval  = 2;
  int bw_interval    = 1;
  int cpu_interval   = 2;
  int mem_interval   = 2;
  int disk_interval  = 30;
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
  for (;;sleep(1))
    {
      if (counter % conn_interval == 0)  conn = getconnection();
      if (counter % bw_interval == 0)    bw   = getbandwidth();
      if (counter % cpu_interval == 0)   cpu  = getcpuload();
      if (counter % mem_interval == 0)   mem  = getmeminfo();
      if (counter % disk_interval == 0)  disk = getdiskusage();
      if (counter % temp_interval == 0)  temp = gettemperature();
      if (counter % batt_interval == 0)  batt = getbattery();
      if (counter % tmloc_interval == 0) tmloc = mktimes("%Y-%m-%d %H:%M:%S %Z");

      status = smprintf("%s %s | %s | %s | %s | %s | %s | %s",
			conn, bw, cpu, mem, disk, temp, batt, tmloc);
      setstatus(status);
	    
      counter = (counter + 1) % max_interval;
      if (counter % conn_interval == 0)  free(conn);
      if (counter % bw_interval == 0)    free(bw);
      if (counter % cpu_interval == 0)   free(cpu);
      if (counter % mem_interval == 0)   free(mem);
      if (counter % disk_interval == 0)  free(disk);
      if (counter % temp_interval == 0)  free(temp);
      if (counter % batt_interval == 0)  free(batt);
      if (counter % tmloc_interval == 0) free(tmloc);
	    
      free(status);
    }
	
  XCloseDisplay(dpy);
	
  return 0;
}

