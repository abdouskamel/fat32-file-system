#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define DIRECTORY_ENTRY 0
#define FILE_ENTRY 1

#define BIOS_PARAM_OFFSET 11
#define FAT32_INFO_OFFSET 36

#define DIR_ENTRY_SIZE 32
#define DIR_ENTRY_ATTR 11

#define GET_CLUSTER(high, low) (((high) << 16) + (low))
#define IS_DIRECTORY(flags) ((flags) & (1 << 4))

#define SHORT_NAME_SIZE 8
#define SHORT_EXT_SIZE 3

/*
 * BIOS parameters, located in boot section.
 */
struct bios_param
{
    uint16_t sector_size;
    uint8_t cluster_size;
    uint16_t nb_reserved_sectors;
    uint8_t nb_fats;
    uint32_t _not_used;
    uint8_t support_descriptor;
    uint16_t _not_used_;
    uint16_t nb_sectors_track;
    uint16_t nb_heads;
    uint32_t nb_sectors_before;
    uint32_t nb_sectors_disk;

} __attribute__((packed));

/*
 * FAT32 metadata, located in boot section.
 */
struct fat32_info
{
    uint32_t fat32_nb_sectors;
    uint16_t flags;
    uint16_t version;
    uint32_t root_rep;
    uint16_t fsinfo_sector;
    uint16_t boot_sector_copy;
    uint8_t _not_used[12];
    uint8_t bios_driver;
    uint8_t _not_used_;
    uint8_t boot_sign;
    uint32_t serial_nb;
    char vol_label[11];
    char sysfs_lab[8];

} __attribute__((packed));

/*
 * Short format entry in a directory.
 */
struct short_form_entry
{
    char name[SHORT_NAME_SIZE];
    char ext[SHORT_EXT_SIZE];

    uint8_t flags;
    uint8_t _not_used;
    uint8_t mil_sec_creation_time;
    uint16_t creation_hour;
    uint16_t creation_date;
    uint16_t last_access;
    uint16_t fat_cluster_num_high;
    uint16_t last_edit_hour;
    uint16_t last_edit_date;
    uint16_t fat_cluster_num_low;
    uint32_t file_size;
};

/*
 * Long format entry in a directory.
 */
struct long_form_entry
{
    uint8_t entry_num;
    char name_1_5[10];
    uint8_t flags;
    uint8_t _not_used;
    uint8_t checksum;
    char name_6_11[12];
    uint16_t _not_used_;
    char name_12_13[4];
};

/*
 * BIOS parameters and FAT32 metadata of current FAT32 disk.
 */
struct bios_param bios_param;
struct fat32_info fat32_info;

// The FAT32 and its size
uint32_t *fat32_array;
uint32_t fat32_size;

// Used to store a cluster in main memory
uint8_t *cluster_buff;

// Beginning of the clusters area
uint64_t clusters_area_offset;

// Cluster size in octets
uint32_t cluster_size;

/*
 * Read BIOS parameters and FAT32 metadata of the given disk.
 * return 0 on success, -1 on failure.
 */
int read_fat32_metadata(FILE *disk);

/*
 * Read the file allocation table of the given disk.
 * return 0 on success, -1 on failure.
 */
int read_fat32_array(FILE *disk);

/*
 * Return the first cluster of the given file  and the file_size.
 * filepath must be an absolute path.
 * On failure, return 0.
 */
uint32_t get_file_start_cluster(FILE *disk, char *filepath, uint32_t *ret_file_size);

/*
 * Search for the given token is the directory located in cur_cluster.
 * When found, store in cur_cluster the start cluster of the token.
 * If token is a directory, return DIRECTORY_ENTRY.
 * If token is a file, return FILE_ENTRY.
 * If token is not found, return -1;
 */
int get_entry_start_cluster(FILE *disk, char *token, uint32_t *cur_cluster, uint32_t *size);

/*
 * Compare the two short format entries a and b.
 * Return the size of b if they are equal, -1 otherwise.
 */
int short_form_cmp(char *a, char *b, int maxlen);

/*
 * Compare the long format entry a with the string b.
 * Return the number of characters that have been compared if they are equal,
 * -1 otherwise.
 */
int long_form_cmp(struct long_form_entry *a, char *b);

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage : %s <disk> <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *disk = fopen(argv[1], "rb");
    if (disk == NULL)
    {
        fprintf(stderr, "Can't open %s.\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (read_fat32_metadata(disk) == -1)
    {
        fprintf(stderr, "Can't read metadata of %s.\n", argv[1]);
        fclose(disk);
        return EXIT_FAILURE;
    }

    if (read_fat32_array(disk) == -1)
    {
        fprintf(stderr, "Can't read FAT32 of %s.\n", argv[1]);
        fclose(disk);
        return EXIT_FAILURE;
    }

    cluster_buff = malloc(cluster_size);
    if (cluster_buff == NULL)
    {
        fprintf(stderr, "Internal error, program will close.\n");
        free(fat32_array);
        fclose(disk);
        return EXIT_FAILURE;
    }

    uint32_t file_size = 0;
    uint32_t start_cluster = get_file_start_cluster(disk, argv[2], &file_size);
    int nb_clusters = 0;

    if (start_cluster == 0)
        printf("File not found.\n");

    else
    {
        printf("Clusters:\n");
        while (start_cluster != 0xfffffff)
        {
            printf("%d\n", start_cluster);
            start_cluster = fat32_array[start_cluster];
            nb_clusters++;
        }

        printf("\nNumber of clusters: %d\n", nb_clusters);
        printf("File size: %d octets\n", file_size);
    }

    free(cluster_buff);
    free(fat32_array);
    fclose(disk);
    return EXIT_SUCCESS;
}

/*
 * Read BIOS parameters and FAT32 metadata of the given disk.
 * return 0 on success, -1 on failure.
 */
int read_fat32_metadata(FILE *disk)
{
    if (fseek(disk, BIOS_PARAM_OFFSET, SEEK_SET) == -1)
        return -1;

    if (fread(&bios_param, sizeof(struct bios_param), 1, disk) != 1)
        return -1;

    if (fread(&fat32_info, sizeof(struct fat32_info), 1, disk) != 1)
        return -1;

    // It's better to have cluster_size in octets
    cluster_size = bios_param.cluster_size * bios_param.sector_size;

    fat32_size = fat32_info.fat32_nb_sectors * bios_param.sector_size;
    clusters_area_offset = bios_param.sector_size * bios_param.nb_reserved_sectors + fat32_size * bios_param.nb_fats;
    return 0;
}

/*
 * Read the file allocation table of the given disk.
 * return 0 on success, -1 on failure.
 */
int read_fat32_array(FILE *disk)
{
    fat32_array = malloc(sizeof(uint32_t) * fat32_size);
    if (fat32_array == NULL)
        return -1;

    if (fseek(disk, bios_param.sector_size * bios_param.nb_reserved_sectors, SEEK_SET) == -1)
    {
        free(fat32_array);
        return -1;
    }

    if (fread(fat32_array, fat32_size, 1, disk) != 1)
    {
        free(fat32_array);
        return -1;
    }

    return 0;
}

/*
 * Return the first cluster of the given file and the file_size.
 * filepath must be an absolute path.
 * On failure, return 0.
 */
uint32_t get_file_start_cluster(FILE *disk, char *filepath, uint32_t *ret_file_size)
{
    char *token = strtok(filepath, "/");
    uint32_t cur_cluster = fat32_info.root_rep;
    uint32_t size;

    while (token != NULL)
    {
        int ret = get_entry_start_cluster(disk, token, &cur_cluster, &size);
        if (ret == -1)
            return 0;

        token = strtok(NULL, "/");
        if ((token == NULL && ret == DIRECTORY_ENTRY) ||
            (token != NULL && ret == FILE_ENTRY))
            return 0;
    }

    *ret_file_size = size;
    return cur_cluster;
}

/*
 * Search for the given token in the directory located in cur_cluster.
 * When found, store in cur_cluster the start cluster of the token,
 * and in size the size of this entry.
 * If token is a directory, return DIRECTORY_ENTRY.
 * If token is a file, return FILE_ENTRY.
 * If token is not found, return -1;
 */
int get_entry_start_cluster(FILE *disk, char *token, uint32_t *cur_cluster, uint32_t *size)
{
    uint32_t dir_cluster = *cur_cluster;

    while (1)
    {
        if (fseek(disk, clusters_area_offset + ((dir_cluster - 2) * cluster_size), SEEK_SET) == -1)
            return -1;

        if (fread(cluster_buff, cluster_size, 1, disk) != 1)
            return -1;

        uint8_t *buff_ptr;
        for (buff_ptr = cluster_buff; *buff_ptr != 0; buff_ptr += DIR_ENTRY_SIZE)
        {
            // This entry is removed.
            if (*buff_ptr == 0xe5)
                continue;

            // Long format entry.
            if (buff_ptr[DIR_ENTRY_ATTR] == 0x0f)
            {
                // Get number of entries by removing the sixth bit.
                uint8_t nb_entries = *buff_ptr & 0x1f;

                // Go to the last entry.
                struct long_form_entry *entry = (struct long_form_entry *)(buff_ptr + (nb_entries - 1) * DIR_ENTRY_SIZE);

                int i, ret, tok_pos = 0;
                for (i = 0; i < nb_entries; ++i, --entry)
                {
                    ret = long_form_cmp(entry, token + tok_pos);
                    if (ret == -1)
                        break;

                    tok_pos += ret;
                }

                // The long format entry matches the token.
                if (ret != -1 && token[tok_pos] == '\0')
                {
                    struct short_form_entry *sentry = (struct short_form_entry *)(buff_ptr + nb_entries * DIR_ENTRY_SIZE);
                    *cur_cluster = GET_CLUSTER(sentry->fat_cluster_num_high, sentry->fat_cluster_num_low);
                    *size = sentry->file_size;

                    if (IS_DIRECTORY(sentry->flags))
                        return DIRECTORY_ENTRY;

                    else
                        return FILE_ENTRY;
                }

                buff_ptr += nb_entries * DIR_ENTRY_SIZE;
            }

            // Short format entry.
            else
            {
                struct short_form_entry *entry = (struct short_form_entry *)buff_ptr;
                int ret = short_form_cmp(entry->name, token, SHORT_NAME_SIZE);
                if (ret != -1)
                {
                    // Here, it has an extension.
                    if (token[ret] == '.')
                        token++;

                    if (short_form_cmp(entry->ext, token + ret, SHORT_EXT_SIZE) != -1)
                    {
                        *cur_cluster = GET_CLUSTER(entry->fat_cluster_num_high, entry->fat_cluster_num_low);
                        *size = entry->file_size;

                        if (IS_DIRECTORY(entry->flags))
                            return DIRECTORY_ENTRY;

                        else
                            return FILE_ENTRY;
                    }
                }
            }
        }

        break;
    }

    return -1;
}

/*
 * Compare the two short format entries a and b.
 * Return the size of b if they are equal, -1 otherwise.
 */
int short_form_cmp(char *a, char *b, int maxlen)
{
    int i;
    for (i = 0; i < maxlen && a[i] != ' ' && b[i] != '.' && b[i] != '\0'; ++i)
    {
        if (a[i] != toupper(b[i]))
            return -1;
    }

    if ((b[i] == '.' || b[i] == '\0') && (a[i] == ' ' || i == maxlen))
        return i;

    else
        return -1;
}

/*
 * Compare the long format entry a with the string b.
 * Return the number of characters that have been compared if they are equal,
 * -1 otherwise.
 */
int long_form_cmp(struct long_form_entry *a, char *b)
{
    int i, ret = 0;

    // Compare characters from 1 to 5. 
    for (i = 0; i < 10 && a->name_1_5[i] != '\0' && b[ret] != '\0'; i += 2, ++ret)
    {
        if (b[ret] != a->name_1_5[i])
            return -1;
    }

    if (i < 10)
    {
        if (a->name_1_5[i] != b[ret])
            return -1;

        else
            return ret;
    }

    // Compare characters from 6 to 11. 
    for (i = 0; i < 12 && a->name_6_11[i] != '\0' && b[ret] != '\0'; i += 2, ++ret)
    {
        if (b[ret] != a->name_6_11[i])
            return -1;
    }

    if (i < 12)
    {
        if (a->name_6_11[i] != b[ret])
            return -1;

        else
            return ret;
    }

    // Compare characters from 12 to 13. 
    if (a->name_12_13[0] != b[ret])
        return -1;

    if (b[ret] == '\0')
        return ret + 1;

    ++ret;
    if (a->name_12_13[2] != b[ret])
        return -1;

    return ret + 1;
}
