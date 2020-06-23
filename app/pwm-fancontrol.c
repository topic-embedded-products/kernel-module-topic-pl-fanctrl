#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void usage(const char *prog)
{
	printf(
		"Usage: %s [-d] [-v] [-i pidfile] -t temperature -p pwm\n"
		"-d  Deamonize, run in background\n"
		"-v  Verbose mode\n"
		"-i  Write process ID into pidfile\n"
		"-t  File to read temperature from (hwmon)\n"
		"-p  File to write PWM value to (hwmon)\n"
		"\n", prog);
}

static int read_sys_file_int(const char* filename, int* value)
{
	int err;
	FILE* f = fopen(filename, "r");

	if (f == NULL)
		return -errno;
	err = fscanf(f, "%d", value);
	fclose(f);
	if (err != 1)
		return (err < 0) ? err : -EINVAL;
	return 0;
}

static int write_sys_file_int(const char* filename, int value)
{
	int err;
	FILE* f = fopen(filename, "w");

	if (f == NULL)
		return -errno;
	err = fprintf(f, "%d", value);
	fclose(f);
	if (err != 1)
		return (err < 0) ? err : -EINVAL;
	return 0;
}


static int read_sys_file_float(const char* filename, float* value)
{
	int err;
	FILE* f = fopen(filename, "r");

	if (f == NULL)
		return -errno;
	err = fscanf(f, "%f", value);
	fclose(f);
	if (err != 1)
		return (err < 0) ? err : -EINVAL;
	return 0;
}

void signal_handler(int sig)
{
	switch(sig) {
	case SIGHUP:
		break;
	case SIGTERM:
		exit(0);
		break;
	}
}

static void daemonize(const char *lock_file)
{
	int i;
	int lfp;
	char str[10];

	if (getppid() == 1)
		return;

	i = fork();
	if (i < 0)
		exit(1);

	if (i > 0)
		exit(0);

	setsid();

	for(i = getdtablesize(); i >= 0; --i)
		close(i);

	i = open("/dev/null", O_RDWR);
	dup(i);
	dup(i);
	umask(027);

	chdir("/"); /* Don't block mount points etc */

	if (lock_file) {
		lfp = open(lock_file, O_RDWR | O_CREAT, 0640);
		if (lfp < 0)
			exit(1);

		if (lockf(lfp, F_TLOCK, 0) < 0)
			exit(1);

		sprintf(str, "%d\n", getpid());
		write(lfp, str, strlen(str));
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);
}

int main(int argc, char * const *argv)
{
	const char *pwm_file = NULL;
	const char *temp_file = NULL;
	const char *lock_file = NULL;
	int verbose = 0;
	int deamon = 0;
	int opt;
	int cpu_temp;
	int fan_pwm;
	int cpu_fan_pwm;
	int r;
	int i;
	int watchdog_fd;

	while ((opt = getopt(argc, argv, "di:p:t:v")) != -1) {
		switch (opt) {
		case 'd':
			deamon = 1;
			break;
		case 'i':
			lock_file = optarg;
			break;
		case 'p':
			pwm_file = optarg;
			break;
		case 't':
			temp_file = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default: /* '?' */
			usage(argv[0]);
			return 1;
		}
	}

	if (!pwm_file || !temp_file) {
		printf("ERROR: PWM and temp filenames are mandatory\n\n");
		usage(argv[0]);
		return 1;
	}

	r = read_sys_file_int(pwm_file, &fan_pwm);
	if (r < 0)
		fan_pwm = 0; /* assume the worst */

	if (deamon)
		daemonize(lock_file);

	/* The deamon part */
	watchdog_fd = open("/dev/watchdog", O_WRONLY);

	for(;;) {
		r = read_sys_file_int(temp_file, &cpu_temp);
		if (r < 0)
			cpu_temp = 100000; /* assume the worst */

		/* Target for CPU temp is ~70 degrees, so PWM=255 at 80 and PWM=25 at 50 */
		cpu_fan_pwm = (cpu_temp - 46850) / 130;
		if (verbose)
			printf("CPU %d, PWM=%d\n", cpu_temp, cpu_fan_pwm);
		if (cpu_fan_pwm > 255)
			cpu_fan_pwm = 255;
		if (cpu_fan_pwm < 25)
			cpu_fan_pwm = 25;

		if (cpu_fan_pwm != fan_pwm) {
			write_sys_file_int(pwm_file, cpu_fan_pwm);
			fan_pwm = cpu_fan_pwm;
		}

		if (watchdog_fd != -1)
			write(watchdog_fd, "x", 1);

		sleep(1);
	}

	/* We won't actually get here */
	return 0;
}
