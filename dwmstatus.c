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
  CPU usage - Load average followed by average CPU load
  CPU temperatures - Coloured red above a threshold
  Battery indicator - color dependadnt with icon depending on state.
  Date and time
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

int num_cpus;

void
getnumcpus(void)
{
  num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  if (num_cpus<1)        /* SPARC glibc is buggy */
    num_cpus=1;
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

typedef struct CT_t
{
  unsigned long long u, n, s, i, w, x, y, z;
  unsigned long long tot;
  unsigned long long edge;
} CT_t;

char *
cpuload(void)
{
  static FILE *stat_file = NULL;
  static char *buf;
  static CT_t tics_prv;
  static CT_t tics_cur;
  static CT_t tics_frme;
  static int siz = -1;
  int tot_read, num;
  float scale;
  float pct_tot;
  
  if (!stat_file)
    {
      if (!(stat_file = fopen("/proc/stat", "r")))
	perror("cpuload failed to open /proc/stat");
    }
  rewind(stat_file);
  fflush(stat_file);
  
#define buffGRW 1024
  
  if (buf)
    {
      buf[0] = '\0';
    }
  else
    {
      buf = calloc(1, (siz = buffGRW));
    }
  while (0 < (num = fread(buf + tot_read, 1, (siz - tot_read), stat_file)))
    {
      tot_read += num;
      if (tot_read < siz)
	break;
      buf = realloc(buf, (siz += buffGRW));
    };
  
  buf[tot_read] = '\0';
  
#undef buffGRW

  /* Read in the values */
  memcpy(&tics_prv, &tics_cur, sizeof(CT_t));
  // then value the last slot with the cpu summary line
  if (4 > sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu"
		 , &tics_cur.u, &tics_cur.n, &tics_cur.s
		 , &tics_cur.i, &tics_cur.w, &tics_cur.x
		 , &tics_cur.y, &tics_cur.z))
    perror("cpuload failed to read CPU tic values");

  tics_cur.tot = tics_cur.u + tics_cur.s
    + tics_cur.n + tics_cur.i + tics_cur.w
    + tics_cur.x + tics_cur.y + tics_cur.z;
  /* if a cpu has registered substantially fewer tics than those expected,
     we'll force it to be treated as 'idle' so as not to present misleading
     percentages. */
#define TICS_EDGE  20
  tics_cur.edge =
    ((tics_cur.tot - tics_prv.tot) / num_cpus) / (100 / TICS_EDGE);

  /* Calculate percentages */
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

  return smprintf("%6.2f", pct_tot);
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
	char *avgs;
	char *cpu;
	char *tmloc;

	int counter = 0;
	int avgs_interval  = 2;
	int cpu_interval   = 2;
	int tmloc_interval = 1;
	int max_interval   = 2;

	/* Run functions that have to be run only once */
	getnumcpus();

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	/* Regular updates */
	for (;;sleep(1)) {
	        if (counter % avgs_interval == 0) avgs = loadavg();
	        if (counter % cpu_interval == 0) cpu = cpuload();
		if (counter % tmloc_interval == 0)
		  tmloc = mktimes("%Y-%m-%d %H:%M:%S %Z");

		status = smprintf(":%s %s% | %s",
				  avgs, cpu, tmloc);
		setstatus(status);

		if (!avgs)  free(avgs);
		if (!cpu)   free(cpu);
		if (!tmloc) free(tmloc);
		free(status);

		counter = (counter + 1) % max_interval;
	}

	XCloseDisplay(dpy);

	return 0;
}

