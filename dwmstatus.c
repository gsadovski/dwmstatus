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

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
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

int
main(void)
{
	char *status;
	char *avgs;
	char *tmloc;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
		avgs = loadavg();
		
		tmloc = mktimes("%Y-%m-%d %H:%M:%S %Z");

		status = smprintf("L:%s | %s",
				  avgs, tmloc);
		setstatus(status);
		free(avgs);
		free(tmloc);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

