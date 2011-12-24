#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <openssl/md5.h>
#include <sys/mman.h>
#include <fcntl.h>


unsigned char md5sum[MD5_DIGEST_LENGTH];


#define NAME "dupf"
#define VERSION "0.1"

#define NODEBUG

#ifdef DEBUG
#   define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#   define PDEBUG(fmt, args...)
#endif


typedef struct args {
    char* dir;
} args_t;

typedef struct _file {
    struct _file *next;
    char *md5sum;
    char *name;
} file_t;

typedef struct _fsize {
    struct _fsize *next;
    off_t size; 
	int count;
    struct _file *file;
} fsize_t;

void free_file (file_t *file) {
    while(file) {
        file_t *next = file->next;
        free(file->name);
		//free(file->md5sum);
        free(file);
        file = next;
    }
}

void free_fsize (fsize_t *fsize) {
    while(fsize != NULL) {
        fsize_t *next = fsize->next;
        free_file(fsize->file);
        free(fsize);
        fsize = next;
    }
}

void usage() {
    printf("Usage: " NAME " -d <dir>\n");
    printf("Description:\n");
    printf("    Find files in a folder.:\n");
}

void get_arguments(int argc, char** argv, args_t* arguments) {
    opterr = 0;
    int c;

    while((c = getopt(argc, argv, "d:")) != -1) {
        switch (c) {
            case 'd':
                arguments->dir = optarg;
                break;
            case '?':
                if(optopt == 'd')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                usage();
                exit(EXIT_FAILURE);
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }
}

void print_md5sum(unsigned char* md) {
    int i;
    for(i=0; i <MD5_DIGEST_LENGTH; i++) {
            printf("%02x",md[i]);
    }
	printf("\n");
}

char* md5sum_file(char *filename, off_t file_size) {
    int file_descript;
    char* file_buffer;

    file_descript = open(filename, O_RDONLY);
    if(file_descript < 0) exit(-1);

	file_buffer = mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0);
    MD5((unsigned char*) file_buffer, file_size, md5sum);
	close(file_descript);
	//printf("%s ", filename);
	//print_md5_sum(md5sum);

	return md5sum;
}

void traverse_fsize_list (fsize_t *size_db) {
    fsize_t *s;
    file_t *f;

    for(s = size_db; s != NULL; s = s->next) {
        printf("%ld(%d)\n", s->size, s->count);
        for(f = s->file; f != NULL; f = f->next) {
            if(f->next == NULL) {
                printf("└─%s ", f->name);
            } else {
                printf("├─%s ", f->name);
            }
			print_md5sum(f->md5sum);
        }
    }
}

fsize_t* find_size(off_t size, fsize_t *current) {
    while(current) {
       if(current->size == size) {
           return current;
       } else {
           current = current->next;
       }
    }

    // current will be NULL
    return current;
}

void free_single_size (fsize_t *target, fsize_t *fsize) {
	fsize_t *target_next = target->next;

	free_file(target->file);
	while(fsize) {
		fsize_t *next = fsize->next;
		if(next == target) {
			fsize_t *next = fsize->next;
			fsize->next = target_next;
			//free(target);
			break;
		}
		fsize = next;
	}
}

void md5sum_same_size(fsize_t *size_db) {
	fsize_t *s;
	file_t *f;

	for(s = size_db; s != NULL; s = s->next) {
		if(s->count == 1) {
			// remove list with single file, don't do it for first
			// element (i.e 0) otherwise we'll have dangling pointer
			free_single_size(s, size_db);
		} else {
			for(f = s->file; f != NULL; f = f->next) {
				f->md5sum = strdup(md5sum_file(f->name, s->size));
			}

		}
	}
}

void sort_with_size(char* filename, fsize_t *size_db) {
    struct stat attributes;
    fsize_t *found_size;

    stat(filename, &attributes);
    found_size = find_size(attributes.st_size, size_db);

    if(found_size) {
        file_t *current = malloc(sizeof(file_t));
        if(!current) {
            fprintf(stderr, "FATAL: Unable to allocated memory.\n");
            exit(EXIT_FAILURE);
        }
        current->name = strdup(filename);

        // Check because the first object will not have memory allocated for file structure
        if(found_size->file) {
            current->next = found_size->file->next;
            found_size->file->next = current;
        } else {
            current->next = NULL;
            found_size->file = current;
        }
		found_size->count++;
    } else {
        fsize_t *current = malloc(sizeof(fsize_t));
        if(!current) {
            fprintf(stderr, "FATAL: Unable to allocated memory.\n");
            exit(EXIT_FAILURE);
        }
        current->size = attributes.st_size;
        current->next = size_db->next;
        size_db->next = current;

        current->file = malloc(sizeof(fsize_t));
        if(!current->file) {
            fprintf(stderr, "FATAL: Unable to allocated memory.\n");
            exit(EXIT_FAILURE);
        }
        current->file->name = strdup(filename);
        current->file->next = NULL;
		current->count = 1;
    }
}

void get_files_list(char* path, fsize_t *size_db) {
    DIR *dir;
    struct dirent *entry;
    char *apath;

    if(dir = opendir(path)) {
        while(entry = readdir(dir)) {
            // Skip .. & .
            if(strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) 
                continue;

            // Size + 2 for backslash and null termination
            int len = strlen(path) + strlen(entry->d_name) + 2;
            apath = malloc(len);
            if(apath) {
                apath[len - 1] = '0';

                // Make absolute path of file
                snprintf(apath, len, "%s/%s", path, entry->d_name);
            } else {
                fprintf(stderr, "ERROR: Unable to allocate memory (%s).\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            // Read directory recursively
            if(entry->d_type == DT_DIR) {
                get_files_list(apath, size_db);
            } else if(entry->d_type == DT_REG) {
                PDEBUG("File: %s/%s\n", path, entry->d_name);        
                sort_with_size(apath, size_db);
            }
            free(apath);
        }
        closedir(dir);
    } else {
        fprintf(stderr, "ERROR: Unable to open directory \"%s\" (%s).\n", path, strerror(errno));
    }
}

int main(int argc, char **argv) {
    args_t arguments;
    fsize_t size_db;

    // Initialize the object
    size_db.file = NULL;
    size_db.size = 0;
	size_db.count = 0;
    size_db.next = NULL;
    
    get_arguments(argc, argv, &arguments);

    printf("Scanning directory: %s\n", arguments.dir);
    get_files_list(arguments.dir, &size_db);
	md5sum_same_size(&size_db);
    traverse_fsize_list(&size_db);
}

