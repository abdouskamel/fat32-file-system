# File Allocation Table (FAT32)

Low-level implementation of the FAT32 file system. Given a disk formatted in FAT32 and a file name, the program reads the file allocation table and prints some information about the file (file size, number of clusters, clusters start addresses).

Compile by running:

```
gcc -o fat32 fat32.c
```

For example, you can try the program on a flash drive formatted in FAT32:

1. Insert a flash drive formatted in fat32.
2. Find the mount point of the flash drive in /dev. To do this, print kernel logs by running:

```
sudo dmesg
```
Output example:
```
[ 8283.811075] usb-storage 3-5:1.0: USB Mass Storage device detected
[ 8283.811447] scsi host0: usb-storage 3-5:1.0
[ 8284.813371] scsi 0:0:0:0: Direct-Access     Verbatim STORE N GO       1.00 PQ: 0 ANSI: 2
[ 8284.813936] sd 0:0:0:0: Attached scsi generic sg0 type 0
[ 8284.814658] sd 0:0:0:0: [sda] 15669248 512-byte logical blocks: (8.02 GB/7.47 GiB)
[ 8284.814906] sd 0:0:0:0: [sda] Write Protect is off
[ 8284.814909] sd 0:0:0:0: [sda] Mode Sense: 03 00 00 00
[ 8284.815153] sd 0:0:0:0: [sda] No Caching mode page found
[ 8284.815155] sd 0:0:0:0: [sda] Assuming drive cache: write through
[ 8284.818524]  sda: sda1 <-- THE NAME OF THE FLASH DRIVE IN /DEV
[ 8284.818832] sd 0:0:0:0: [sda] Attached SCSI removable disk
```
In this case, the flash drive has been mounted to /dev/sda1.

3. Run the program by giving the mount point and a file name (root privileges are necessary to read /dev/sda1):
```
sudo ./fat32 /dev/sda1 path/to/file
```

4. Output example:
```
Clusters:
832926
832927
832928
...

Number of clusters: 556
File size: 2275208 octets
```