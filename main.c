#include "boot_control_definition.h"
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
struct bootloader_control bootctl;
#ifdef __ANDROID__
#include <endian.h>
char MISC_PARTITION[50];
#else
#define MISC_PARTITION "misc.img"
#endif

static uint32_t CRC32(const uint8_t *buf, size_t size)
{
    static uint32_t crc_table[256];

    // Compute the CRC-32 table only once.
    if (!crc_table[1])
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; ++j)
            {
                uint32_t mask = -(crc & 1);
                crc = (crc >> 1) ^ (0xEDB88320 & mask);
            }
            crc_table[i] = crc;
        }
    }

    uint32_t ret = -1;
    for (size_t i = 0; i < size; ++i)
    {
        ret = (ret >> 8) ^ crc_table[(ret ^ buf[i]) & 0xFF];
    }

    return ~ret;
}

// Return the little-endian representation of the CRC-32 of the first fields
// in |boot_ctrl| up to the crc32_le field.
uint32_t BootloaderControlLECRC(const struct bootloader_control *boot_ctrl)
{
    return htole32(
        CRC32((uint8_t *)boot_ctrl, offsetof(struct bootloader_control, crc32_le)));
}
void dumpSlot(struct bootloader_control *ctl)
{
    char slots[][3] = {"_a", "_b", "_c", "_d"};
    char status[][20] = {"none", "unknown", "snapshotted", "merging", "cancelled"};
    // struct slot_metadata *tmp;
    int next_slot = 0;
    for (int i = 1; i < ctl->nb_slot; i++)
    {
        if (ctl->slot_info[i].priority > ctl->slot_info[next_slot].priority)
        {
            next_slot = i;
        }
    }
    fprintf(stdout,
            "next-boot-slot-name:%s\n\
recovery_tries_remaining:%d\n\
merge_status:%s\n\
",
            slots[next_slot],
            ctl->recovery_tries_remaining,
            status[ctl->merge_status]);
    for (int i = 0; i < ctl->nb_slot; ++i)
    {
        fprintf(stdout,
                "slot%s:\n\
        slot-priority:%d (0 means this slot will never be automatically switched !)\n\
        successful_boot:%d (1 means this slot was marked bootable by android)\n\
        tries_remaining:%d \n\
        verity_corrupted:%d\n",
                slots[i],
                ctl->slot_info[i].priority,
                ctl->slot_info[i].successful_boot,
                ctl->slot_info[i].tries_remaining,
                ctl->slot_info[i].verity_corrupted);
    }
}
int saveBootCtrl(struct bootloader_control *ctl)
{
    int fd = open(MISC_PARTITION, O_WRONLY);
    if (fd < 0)
    {
        perror("open fail:");
        return -1;
    }
    if (lseek(fd, OFFSETOF_SLOT_SUFFIX, SEEK_SET) < 0)
    {
        fprintf(stderr, "lseek error\n");
        close(fd);
        return -1;
    }
    ctl->crc32_le = BootloaderControlLECRC(ctl);
    write(fd, ctl, sizeof(struct bootloader_control));
    close(fd);
    return 0;
}
int readBootCtrl(struct bootloader_control *ctl)
{
    int fd = open(MISC_PARTITION, O_RDONLY);
    if (fd < 0)
    {
        perror("open fail:");
        return -1;
    }
    struct bootloader_control buf;
    if (lseek(fd, OFFSETOF_SLOT_SUFFIX, SEEK_SET) < 0)
    {
        fprintf(stderr, "lseek error\n");
        close(fd);
        return -1;
    }
    read(fd, &buf, sizeof(struct bootloader_control));
    if (BootloaderControlLECRC(&buf) != buf.crc32_le)
    {
        fprintf(stderr, "crc mismatch\n");
        close(fd);
        return -2;
    }
    memcpy(ctl, &buf, sizeof(struct bootloader_control));
    return 0;
}
int enterProtect(struct bootloader_control *ctl, int slot)
{
    // 设置tries_remaining
    ctl->slot_info[slot].tries_remaining = BOOT_CONTROL_MAX_RETRY - 1;
    // 设置未能成功启动，触发tries_remaining
    ctl->slot_info[slot].successful_boot = 0;
    return 0;
}
int setActive(struct bootloader_control *ctl, int slot)
{
    ctl->slot_info[slot].priority = BOOT_CONTROL_MAX_PRI;
    ctl->slot_info[slot].tries_remaining = BOOT_CONTROL_MAX_RETRY;
    for (int i = 0; i < ctl->nb_slot; i++)
    {
        if (i == slot)
        {
            continue;
        }
        if (ctl->slot_info[i].priority >= ctl->slot_info[slot].priority)
        {
            ctl->slot_info[i].priority = BOOT_CONTROL_MAX_PRI - 1;
        }
    }
    return 0;
}
static char usage_msg[] =
    "usage: abtool [options]\n"
    "\n"
    "	abtool is a tool to modify ab slot metadata\n"
    "	tool will dump ab infos if no option set\n"
    "\n"
    "	options:\n"
    "		-s <slot>		set slot active\n"
    "		-p <slot>		set successful_boot=0 and tries_remaining=BOOT_CONTROL_MAX_RETRY - 1\n"
    "		-d		dump abmetadata.\n"
    "		-h		show this message and exit.\n";
void help()
{
    fprintf(stdout, "%s", usage_msg);
}
#ifdef __ANDROID__
void getMiscPartition()
{
    char buf[50];
    int r;
    memset(buf, 0, 50);
    if ((r = readlink("/dev/block/by-name/misc", buf, 50)) > 0)
    {
        buf[r] = '\0';
        strcpy(MISC_PARTITION, buf);
    }
    else
    {
        perror("get misc partition fail:");
        exit(-1);
    }
}
#endif
#define FLAG_DUMP 0b000001
#define FLAG_SET_ACTIVE 0b000010
#define FLAG_PROTECT 0b000100
int main(int argc, char *const *argv)
{
    char c;
    int mode = 0, active_slot, protect_slot;
    while ((c = getopt(argc, argv, "hdp:s:")))
    {
        switch (c)
        {
        case 'h':
            help();
            return 0;
            continue;
        case 'd':
            mode |= FLAG_DUMP;
            continue;
        case 'p':
            mode |= FLAG_PROTECT;
            protect_slot = atoi(optarg);
            continue;
        case 's':
            mode |= FLAG_SET_ACTIVE;
            active_slot = atoi(optarg);
            continue;
        }
        break;
    }
    int i = 0;
#ifdef __ANDROID__
    getMiscPartition();
#endif
    if ((i = readBootCtrl(&bootctl)) < 0)
    {
        if (i == -2)
        {
            fprintf(stderr, "unsupported\n");
        }
        return -1;
    }
    else
    {
        if (mode & FLAG_DUMP || mode == 0)
        {
            dumpSlot(&bootctl);
        }
        if (mode & FLAG_PROTECT)
        {
            enterProtect(&bootctl, protect_slot);
        }
        if (mode & FLAG_SET_ACTIVE)
        {
            setActive(&bootctl, active_slot);
        }
        if (mode >= FLAG_SET_ACTIVE)
        {
            saveBootCtrl(&bootctl);
        }
    }
    return 0;
}
