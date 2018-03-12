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
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
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

/* Network info */

#define WIFICARD             "wlp7s0"
#define WIREDCARD            "enp9s0"
#define NETDEV_FILE          "/proc/net/dev"
#define WIFI_OPERSTATE_FILE  "/sys/class/net/"WIFICARD"/operstate"
#define WIRED_OPERSTATE_FILE "/sys/class/net/"WIREDCARD"/operstate"
#define WIRELESS_FILE        "/proc/net/wireless"

int
parsenetdev(unsigned long long *receivedabs, unsigned long long *sentabs)
{
  const int bufsize = 255;
  char buf[bufsize];
  char *datastart;
  FILE *fp = NULL;
  int rval = 1;
  unsigned long long receivedacc, sentacc;

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
	    }
	}

      fclose(fp);
      rval = 0;
    }
  else
    {
      warn("Error opening "NETDEV_FILE);
    }

  return rval;
}

int
getbandwidth(double *downbw, double *upbw)
{
  static unsigned long long rec, sent;
  unsigned long long newrec, newsent;
  newrec = newsent = 0;
  int rval = 1;

  if (parsenetdev(&newrec, &newsent)) return rval;

  *downbw = (newrec  - rec)  / 1024.0;
  *upbw   = (newsent - sent) / 1024.0;

  rec = newrec;
  sent = newsent;

  rval = 0;

  return rval;
}

int
getwifistrength(int *strength)
{
  char buf[255];
  char *datastart;
  FILE *fp = NULL;
  int rval = 1;

  if ((fp = fopen(WIRELESS_FILE, "r")))
    {
      fgets(buf, sizeof(buf), fp);
      fgets(buf, sizeof(buf), fp);
      fgets(buf, sizeof(buf), fp);
      fclose(fp);
    }
  else
    {
      warn("Error opening "WIRELESS_FILE);
      return rval;
    }

  datastart = strstr(buf, WIFICARD":");
  if (datastart != NULL)
    {
      datastart = strstr(buf, ":");
      sscanf(datastart + 1, " %*d   %d  %*d  %*d		  %*d	   %*d		%*d		 %*d	  %*d		 %*d", strength);

      rval = 0;
    }

  return rval;
}

int
getwifiessid(char *id)
{
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  struct iwreq wreq;
  int rval = 1;

  memset(&wreq, 0, sizeof(struct iwreq));
  wreq.u.essid.length = IW_ESSID_MAX_SIZE+1;
  sprintf(wreq.ifr_name, WIFICARD);
  if (sockfd == -1)
    {
      warn("Cannot open socket for interface: %s", WIFICARD);
      return rval;
    }
  wreq.u.essid.pointer = id;
  if (ioctl(sockfd,SIOCGIWESSID, &wreq) == -1)
    {
      warn("Get ESSID ioctl failed for interface %s", WIFICARD);
    }
  else
    {
      rval = 0;
    }
  close(sockfd);

  return rval;
}

char *
bwstr(double bw)
{
  if (bw > 1024.0)
    {
      bw /= 1024.0;
      return smprintf("%.1fmbps", bw);
    }
  else
    {
      return smprintf("%.0fkbps", bw);
    }
}

char *
getconnection(void)
{
  char status[5];
  int eth0, wifi;
  char essid[IW_ESSID_MAX_SIZE+1];
  char *conntype;
  int strength;
  double downbw, upbw;
  char *downstr, *upstr;
  char *connection;

  if (readvaluesfromfile(WIRED_OPERSTATE_FILE, "%s\n", status)) return smprintf("");
  eth0 = !strcmp(status, "up");

  if (readvaluesfromfile(WIFI_OPERSTATE_FILE, "%s\n", status)) return smprintf("");
  wifi = !strcmp(status, "up");

  if (wifi)
    {
      if (getwifiessid(essid))
	{
	  warn("Failed to obtain WiFi ESSID");
	  sprintf(essid, "N/A");
	}
      if (getwifistrength(&strength))
	{
	  warn("Failed to obtain WiFi strength");
	}
    }

  if (eth0)
    {
      if (wifi)
	{
// 	    conntype = smprintf("  %s  %d%%");
	  conntype = smprintf("⚼");
	}
      else
	{
	  conntype = smprintf("⚼");
	}
    }
  else if (wifi)
    {
//      conntype = smprintf(" %s  %d%%", essid, strength);
      conntype = smprintf("📶%d%%", strength);
    }
  else
    {
      return smprintf("offline");
    }

  if(getbandwidth(&downbw, &upbw))
    {
      warn("Failed to obtain bandwidth");
      downstr = "---- KiB/s";
      upstr   = "---- KiB/s";
    }
  else
    {
      downstr = bwstr(downbw);
      upstr   = bwstr(upbw);
    }

  connection =  smprintf("%s ▼%s ▲%s", conntype, downstr, upstr);

  free(upstr);
  free(downstr);
  free(conntype);

  return connection;
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

  return smprintf("🏽%.0f%%(%.2f|%.2f|%.2f)",
		  pct_tot,avgs[0], avgs[1], avgs[2]);
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
  float gb_main_used; /* , gb_main_total; */
  float gb_swap_used; /* , gb_swap_total; */

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
  /* gb_main_total = (float)kb_main_total / 1024 / 1024; */
  gb_swap_used  = (float)kb_swap_used  / 1024 / 1024;
  /* gb_swap_total = (float)kb_swap_total / 1024 / 1024; */

  return smprintf("🧠%1.1f/%1.1fGb",
		  gb_main_used, gb_swap_used);
}

/* Disk info */

/*
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
*/

/*  Temperature info*/

#define TEMP_INPUT "/sys/class/hwmon/hwmon0/temp1_input"
#define TEMP_CRIT  "/sys/class/hwmon/hwmon0/temp1_crit"

char *
gettemperature(void)
{
  long temp, tempc;

  if (readvaluesfromfile(TEMP_INPUT, "%ld\n", &temp)) return smprintf("");
  if (readvaluesfromfile(TEMP_CRIT, "%ld\n", &tempc)) return smprintf("");

  return smprintf("🌡%ld°C", temp / 1000);
}

/* Battery info */

#define BATT_NOW        "/sys/class/power_supply/BAT0/charge_now"
#define BATT_FULL       "/sys/class/power_supply/BAT0/charge_full"
#define BATT_STATUS     "/sys/class/power_supply/BAT0/status"
#define POW_NOW         "/sys/class/power_supply/BAT0/current_now"

#define GLYPH_UNKWN   "?"
#define GLYPH_FULL    "🔌"
#define GLYPH_CHRG    "🗲"  
#define GLYPH_DCHRG_0 "🔋" 
#define GLYPH_DCHRG_1 "🔋"
#define GLYPH_DCHRG_2 "🔋"
#define GLYPH_DCHRG_3 "🔋"
#define GLYPH_DCHRG_4 "🔋"

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
      return smprintf("%s%ld%%(%ld:%ld)", s,pct,hh,mm);
    }
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

/*
char *
mktimestz(char *fmt, char *tzname)
{
  setenv("TZ", tzname, 1);
  char *buf = mktimes(fmt);
  unsetenv("TZ");

  return buf;
}
*/

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
  char *cpu;
  char *mem;
//  char *disk;
  char *temp;
  char *batt;
  char *tmloc;

  int counter = 0;
  int conn_interval  = 1;
  int cpu_interval   = 2;
  int mem_interval   = 2;
//  int disk_interval  = 30;
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
      if (counter % cpu_interval == 0)   cpu  = getcpuload();
      if (counter % mem_interval == 0)   mem  = getmeminfo();
//      if (counter % disk_interval == 0)  disk = getdiskusage();
      if (counter % temp_interval == 0)  temp = gettemperature();
      if (counter % batt_interval == 0)  batt = getbattery();
      if (counter % tmloc_interval == 0) tmloc = mktimes("📆 %a,%d/%m/%Y  ⌛ %H:%M");
//      if (counter % tmloc_interval == 0) tmloc = mktimes("%Y-%m-%D %H:%M:%S %Z");

/*    status = smprintf(" %s | %s | %s | %s | %s | %s | %s",
			conn, cpu, mem, disk, temp, batt, tmloc);
      setstatus(status);
*/
      status = smprintf("%s %s %s %s %s  %s ",
			conn, cpu, mem, temp, batt, tmloc);
      setstatus(status);

      counter = (counter + 1) % max_interval;
      if (counter % conn_interval == 0)  free(conn);
      if (counter % cpu_interval == 0)   free(cpu);
      if (counter % mem_interval == 0)   free(mem);
//      if (counter % disk_interval == 0)  free(disk);
      if (counter % temp_interval == 0)  free(temp);
      if (counter % batt_interval == 0)  free(batt);
      if (counter % tmloc_interval == 0) free(tmloc);

      free(status);
    }

  XCloseDisplay(dpy);

  return 0;
}
