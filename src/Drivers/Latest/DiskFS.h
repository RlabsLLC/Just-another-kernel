#ifndef DISK_FS_H
#define DISK_FS_H

void diskfs_init(void);
int diskfs_mount_or_format(void);
int diskfs_sync(void);
int diskfs_flash(void);
void diskfs_print_info(void);
int diskfs_bootstrap_with_fallback(uint8_t force_ram_vfs, uint32_t timeout_ticks);

#endif