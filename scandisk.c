#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


//Linked list for cluster tracking
struct cnode{
    uint16_t *next;
    uint16_t *status;
    uint16_t *parent;
    };

//Globals *sorry*
struct cnode *clusters[2880];
uint8_t *image_buf;
struct bpb33* bpb;


void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}

void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}

int follow_chain(struct direntry *dirent, uint16_t cluster, uint32_t bytes_left) {
    int total, size;
    size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    total = bpb->bpbSectors / bpb->bpbSecPerClust;
    assert(cluster <= total);

    if (cluster == 0) {
        fprintf(stderr, "Incorrect termination in file\n");
        return 0;
    }
    if ((int)bytes_left < size) {
        clusters[cluster]->parent = getushort(dirent->deStartCluster);
        clusters[cluster]->status = (uint16_t) (FAT12_MASK & CLUST_EOFS);
        if ((int)bytes_left > size) {
            return 1;
        } else {
            return 2;
        }
    } else {
        clusters[cluster]->parent = getushort(dirent->deStartCluster);
        clusters[cluster]->status = 1;
        int i = 0;
        if (is_valid_cluster(get_fat_entry(cluster, image_buf, bpb), bpb)) {
            i = follow_chain(dirent, get_fat_entry(cluster, image_buf, bpb), bytes_left - size);
        }
        if (i > 2) {
            clusters[cluster]->next = i;
        } else if (i == 2) {
            clusters[cluster]->next = get_fat_entry(cluster, image_buf, bpb);
        }
        return cluster;
    }
}

uint16_t print_dirent(struct direntry *dirent, int indent)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
	int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
	int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
	int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
	int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;
        clusters[followclust]->status = 0x0400;
	size = getulong(dirent->deFileSize);
	print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
    }

    return followclust;
}

void check_size(struct direntry *dirent){
    uint16_t cluster = getushort(dirent->deStartCluster);
    uint32_t fsize = getulong(dirent->deFileSize);
    uint32_t total = 0;
    while (is_valid_cluster(cluster, bpb)) {
        total += 1;
        cluster = get_fat_entry(cluster, image_buf, bpb);
    }
    if (fsize % 512 == 0) {
        fsize = fsize % 512;
    } else {
        fsize = (fsize / 512) + 1;
    }
    if (fsize > total) {
        fsize = total * bpb->bpbBytesPerSec;
        putulong(dirent->deFileSize, fsize);
    }
    if (fsize < total) {
        fsize = total * bpb->bpbBytesPerSec;
        putulong(dirent->deFileSize, fsize);
    }
}


void follow_dir(uint16_t cluster, int indent)
{
    while (is_valid_cluster(cluster, bpb))
    {
        if (is_end_of_file(cluster)) return;
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
            
            uint16_t followclust = print_dirent(dirent, indent);
            if (followclust) {
                clusters[followclust]->parent = cluster;
                clusters[cluster]->next = followclust;
                clusters[followclust]->status = 100;
                follow_dir(followclust, indent + 1);
            }
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}

int correct_dirent_size(struct direntry *dirent){
    clusters[2]->status = CLUST_FIRST;
    return follow_chain(dirent, getushort(dirent->deStartCluster), getulong(dirent->deFileSize));
}

void write_dirent(struct direntry *dirent, char *fname, uint16_t s_clust, uint32_t size) {
    char *name;
    char *name2;
    char *newname;
    int len;
    memset(dirent, 0, sizeof(struct direntry));
    newname = strdup(fname);
    name = newname;
    for (int i = 0; i < (int)strlen(fname); i++) {
        if (name[i] == '/' || name[i] == '\\') {
            newname = name + i + 1;
        }
    }
    for (int i = 0; i < (int)strlen(newname); i++) {
        newname[i] = toupper(newname[i]);
    }
    memset(dirent->deName, ' ', 8);
    name2 = strchr(newname, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (name2 != NULL) {
        *name2 = '\0';
        name2++;
        len = strlen(name2);
        if (len > 3) len = 3;
        memcpy(dirent->deExtension, name2, len);
    }
    if (strlen(newname) > 8) {
        newname[8] = '\0';
    }
    memcpy(dirent->deName, newname, strlen(newname));
    free(name);
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, s_clust);
    putulong(dirent->deFileSize, size);
}

void traverse_root()
{
    uint16_t cluster = 0;

    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

    int i = 0;
    for (; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0);
        if (is_valid_cluster(followclust, bpb)) {
            clusters[followclust]->parent = cluster;
            clusters[cluster]->next = followclust;
            follow_dir(followclust, 1);
        }

        dirent++;
    }
}

void map(){
    uint16_t s_clust = -1;
    int j = 1;
    uint32_t size = 0;
    for (int i = 2; i < 2880; i++) {
        if (clusters[i]->status != 0xffff) {
            if (size > 0){
                char fname[12];
                snprintf(fname, 12, "found %d", 42);
                write_dirent((struct direntry*)cluster_to_addr(0, image_buf, bpb), fname, s_clust, size);
                size = 0;
                j++;
            }
            if (get_fat_entry(i, image_buf, bpb) == CLUST_FREE) {
                clusters[i]->status = CLUST_FREE;
            }
        } else {
            if (size == 0) s_clust = i;
            clusters[i]->status = 0xffff & 10;
            size += bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 0; i < 2880; i++) {
        clusters[i] = malloc(sizeof(struct cnode));
        clusters[i]->status = 0xffff;
        clusters[i]->parent = -1;
        clusters[i]->next = -1;
    }
    int fd;
    if (argc < 2) {
	usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    traverse_root();
    map();
    unmmap_file(image_buf, &fd);
    for (int i = 0; i < 2880; i++) {
        free(clusters[i]);
    }
    return 0;
}
