#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "ssd1306.h"

#define IF_NAME        "wlan0"
#define MAX_MSG_LEN    (48)
#define MAX_IP_LEN     (15)
#define MAX_CMD_LEN    (32)
#define I2C_ID         (1)
#define DFONT          (0) /* Default small font */
#define LOADAVG        "/proc/loadavg"
#define THERMAL_ZONE0  "/sys/devices/virtual/thermal/thermal_zone0/hwmon0/temp1_input" /* CPU thermal */
#define THERMAL_ZONE1  "/sys/devices/virtual/thermal/thermal_zone1/hwmon1/temp1_input" /* DDR thermal */

struct tc_stat {
    unsigned long usr;
    unsigned long nice;
    unsigned long sys;
    unsigned long idle;
    unsigned long iowait;
    unsigned long irq;
    unsigned long softirq;
    unsigned long steal;
    unsigned long guest;
    unsigned long guest_nice;
};

struct meminfo {
    unsigned long totalram;
    unsigned long freeram;
};

struct loadavg {
    float loadavg_1min;
    float loadavg_5min;
    float loadavg_15min;
    unsigned int nr_runnable;
    unsigned int nr_scheduling;
    unsigned int pid_newly_task;
};

struct rz_stats {
    float cpu_temp;
    float ddr_temp;
    float mem_usage;
    char ip[MAX_IP_LEN];
    struct meminfo mm;
    struct loadavg load; /* /proc/loadavg */
    struct tc_stat tc; /* time consuming */
};

static void do_get_ip(char *ip)
{
    char *temp = NULL, *netdev_name = IF_NAME;
    int inet_sock;
    struct ifreq ifr;

    inet_sock = socket(AF_INET, SOCK_DGRAM, 0);

    memset(ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    memcpy(ifr.ifr_name, netdev_name, strlen(netdev_name));

    if(0 != ioctl(inet_sock, SIOCGIFADDR, &ifr)) {
        perror("ioctl error");
        return;
    }

    temp = inet_ntoa(((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr);
    memcpy(ip, temp, strlen(temp));

    close(inet_sock);
}

static float __get_temp(char *path)
{
    FILE *fp = NULL;
    unsigned int tmp;

    fp = fopen(path, "r");
    if (!fp) {
        printf("%s ain't exist\n", path);
        return 0;
    }

    fscanf(fp, "%u", &tmp);
    fclose(fp);

    return (float)tmp / 1000;
}

static void do_get_cpu_temp(struct rz_stats *stats)
{
    stats->cpu_temp = __get_temp(THERMAL_ZONE0);
    stats->ddr_temp = __get_temp(THERMAL_ZONE1);
}

static void do_get_cpu_usage(struct rz_stats *stats)
{
    FILE *fp = NULL;
    char cmd[MAX_CMD_LEN] = "cat /proc/stat | head -1";

    fp = popen(cmd, "r");
    fscanf(fp, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
            &stats->tc.usr,
            &stats->tc.nice,
            &stats->tc.sys,
            &stats->tc.idle,
            &stats->tc.iowait,
            &stats->tc.irq,
            &stats->tc.softirq,
            &stats->tc.steal,
            &stats->tc.guest,
            &stats->tc.guest_nice);

    pclose(fp);
}

static void do_get_mem_usage(struct rz_stats *stats)
{
    struct sysinfo sinfo;

    if (!sysinfo(&sinfo)) {
        stats->mm.totalram = sinfo.totalram;
        stats->mm.freeram = sinfo.freeram;
    }

    stats->mem_usage =
        (float)(stats->mm.totalram - stats->mm.freeram) / stats->mm.totalram;
}

static void do_get_loadavg(struct rz_stats *stats)
{
    FILE *fp = NULL;

    fp = fopen(LOADAVG, "r");
    fscanf(fp, "%f %f %f %u/%u %u",
        &stats->load.loadavg_1min,
        &stats->load.loadavg_5min,
        &stats->load.loadavg_15min,
        &stats->load.nr_runnable,
        &stats->load.nr_scheduling,
        &stats->load.pid_newly_task);

    fclose(fp);
}

int main(void)
{
    uint8_t orientation = 0,
            display = 1;
    int ret;
    char line[MAX_MSG_LEN];
    bool IP_CHECKED = false;
    struct rz_stats rz_stat;

    // firstly, ssd1306 configuration
    ret = ssd1306_init(I2C_ID);
    if (ret) {
        printf("no oled attached to /dev/i2c-%d\n", I2C_ID);
        return 1;
    }

    ret += ssd1306_oled_default_config(64, 128);

    // check if oled resolution set properly
    if (ssd1306_oled_load_resolution()) {
        printf("oled resolution not set properly\n");
        goto error;
    }

    // clear the screen
    ret += ssd1306_oled_clear_screen();

    ret += ssd1306_oled_set_rotate(orientation);

    ret += ssd1306_oled_onoff(display);

    while (1) {
        // clear all data
        memset(&rz_stat, 0, sizeof(struct rz_stats));

        // get loadavg
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 0);
        snprintf(line, sizeof(line), "OLED PID: %u",
                getpid());
        ssd1306_oled_write_line(DFONT, line);

        // get local ip
        if (!IP_CHECKED) {
            memset(line, 0, sizeof(line));
            ssd1306_oled_set_XY(0, 1);
            do_get_ip(rz_stat.ip);
            snprintf(line, sizeof(line), "(IP): %s", rz_stat.ip);
            ssd1306_oled_write_line(DFONT, line);
            IP_CHECKED = true;
        }

        // get cpu and ddr temperature
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 2);
        do_get_cpu_temp(&rz_stat);
        snprintf(line, sizeof(line), "CPU: %.1f DDR: %.1f",
                rz_stat.cpu_temp, rz_stat.ddr_temp);
        ssd1306_oled_write_line(DFONT, line);

        // get time spent in usr mode
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 3);
        do_get_cpu_usage(&rz_stat);
        snprintf(line, sizeof(line), "CPU USR: %lu",
                rz_stat.tc.usr);
        ssd1306_oled_write_line(DFONT, line);

        // get time spent in sys mode
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 4);
        do_get_cpu_usage(&rz_stat);
        snprintf(line, sizeof(line), "CPU SYS: %lu",
                rz_stat.tc.sys);
        ssd1306_oled_write_line(DFONT, line);

        // get time spent in irq mode
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 5);
        do_get_cpu_usage(&rz_stat);
        snprintf(line, sizeof(line), "CPU IRQ: %lu",
                rz_stat.tc.irq);
        ssd1306_oled_write_line(DFONT, line);

        // get memory usage
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 6);
        do_get_mem_usage(&rz_stat);
        snprintf(line, sizeof(line), "MEM USAGE: %.4f",
                rz_stat.mem_usage);
        ssd1306_oled_write_line(DFONT, line);

        // get loadavg
        memset(line, 0, sizeof(line));
        ssd1306_oled_set_XY(0, 7);
        do_get_loadavg(&rz_stat);
        snprintf(line, sizeof(line), "R: %u SE: %u",
                rz_stat.load.nr_runnable,
                rz_stat.load.nr_scheduling);
        ssd1306_oled_write_line(DFONT, line);

        sleep(1);
    }

error:
    ssd1306_end();

    return 0;
}
