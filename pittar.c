/*
  BUGS
  - Append meta breaks if you are trying to do it for more than two things. Somethings wrong with
  the while loop in there somewheree
  TODO

  WISHLIST
  - Figure out this sleeping thing
  NOTE
  - Header data is absolute (Use SEEK_SET)
*/

#include "shared.h"

char last_dir[200];
int count = 0;          // number of elements being compressed
int metanum = 0;        // index of where we are in the array of meta data structs
meta * metadata;        // array of metadata structs, malloced in main
float curr_offset = 0;  // tracks current offset from beginning of .pitt file to beginning of metadata

void extract_specific(char * element, char * pitt_file);
char * trimmer(char * str);
char * zTrim(char * str);
byte * lzw_decode(byte *in);
void append_meta(char * out);
void archive_file(char * filename, char * pitt_file);
void archive(char * directory, char * pitt_file);
void traverse(char * curr_directory, int depth);
void rPrintHierarchy(meta * m, int num_elts, char * rootname, int depth);
void printHierarchy(char * pitt_file);
void extract(char * pitt_file);
void update_meta(char * pitt_file, int size);
void extractfile(int offset, int size, int permissions, char * path, FILE * fp);
void append(char * element, char * pitt_file);
void compress(char * element, char * pitt_file);
void usage();

/*   Given a long path ie: home/hi/something/whatever////
**   will return the last element, ie: whatever
*/
char * trimmer(char * str)
{
    char * end = str + strlen(str) - 1;
    if (*end == '/')
    {
        while (*end == '/' && end >= str)
            --end;
        *(end+1) = 0;

        while (*end != '/' && end >= str)
            --end;
        ++end;

        return end;
    }
    else
    {
        while (*end != '/' && end >= str)
            --end;
        ++end;

        return end;
    }
}


/*      Trims the .Z off the end of a file name
**
*/
char * zTrim(char * str)
{
    char * end = str + strlen(str) - 1;
    if(*end == 'Z' && *(end-1) == '.')
    {
        *(end-1) = 0;
    }
    return str;
}

/*      Decodes a byte array
**      Returns a byte array
*/
byte* lzw_decode(byte *in)
{
    byte *out = _new(byte, 4);
    int out_len = 0;

    inline void write_out(byte c)
    {
        while (out_len >= _len(out)) _extend(out);
        out[out_len++] = c;
    }

    lzw_dec_t *d = _new(lzw_dec_t, 512);
    int len, j, next_shift = 512, bits = 9, n_bits = 0;
    ushort code, c, t, next_code = M_NEW;

    uint32_t tmp = 0;
    inline void get_code() {
        while(n_bits < bits) {
            if (len > 0) {
                len --;
                tmp = (tmp << 8) | *(in++);
                n_bits += 8;
            } else {
                tmp = tmp << (bits - n_bits);
                n_bits = bits;
            }
        }
        n_bits -= bits;
        code = tmp >> n_bits;
        tmp &= (1 << n_bits) - 1;
    }

    inline void clear_table() {
        _clear(d);
        for (j = 0; j < 256; j++) d[j].c = j;
        next_code = M_NEW;
        next_shift = 512;
        bits = 9;
    };

    clear_table(); /* in case encoded bits didn't start with M_CLR */
    for (len = _len(in); len;) {
        get_code();
        if (code == M_EOD) break;
        if (code == M_CLR) {
            clear_table();
            continue;
        }

        if (code >= next_code) {
            fprintf(stderr, "Bad sequence\n");
            _del(out);
            goto bail;
        }

        d[next_code].prev = c = code;
        while (c > 255) {
            t = d[c].prev; d[t].back = c; c = t;
        }

        d[next_code - 1].c = c;

        while (d[c].back) {
            write_out(d[c].c);
            t = d[c].back; d[c].back = 0; c = t;
        }
        write_out(d[c].c);

        if (++next_code >= next_shift) {
            if (++bits > 16) {
                /* if input was correct, we'd have hit M_CLR before this */
                fprintf(stderr, "Too many bits\n");
                _del(out);
                goto bail;
            }
            _setsize(d, next_shift *= 2);
        }
    }

    /* might be ok, so just whine, don't be drastic */
    if (code != M_EOD) fputs("Bits did not end in EOD\n", stderr);

    _setsize(out, out_len);
bail:   _del(d);
    return out;
}


/*      Seeks to the end of a .pitt file and appends
**      the current meta data arry to it
*/
void append_meta(char * out)
{
    int i = 0;
    struct stat st;
    header h;

    FILE *fp = fopen(out, "r+");
    fseek(fp, 0, SEEK_SET);
    fread(&h, sizeof(header), 1, fp);
    fseek(fp, 0, SEEK_END);
    fwrite(metadata, sizeof(meta), h.num_elts, fp);
    fclose(fp);
}


/*      Turns a single file in to a .pitt.
**
*/
void archive_file(char * filename, char * pitt_file)
{
    // Declarations
    struct stat st;
    header header;
    FILE * fp;
    int fd;
    count = 1;

    // Get stat information of this .Z for meta data
    stat(filename, &st);

    // Create header
    header.meta_offset = sizeof(header) + st.st_size;                  // is curr_offset viable right now -- no it is not
    header.next = -1;
    header.num_elts = 1;

    // write the header
    fp = fopen(pitt_file, "w+");
    fwrite(&header, sizeof(header), 1, fp);
    fclose(fp);

    curr_offset += sizeof(header);

    // Create proper name of file (without the .Z)
    char * properName = (char *) malloc(strlen(filename));
    strcpy(properName, filename);
    zTrim(properName);

    // Update metadata information about this file
    metanum++;
    strcpy(metadata[0].name, filename);
    strcpy(metadata[0].name_trim, properName);
    metadata[0].size = st.st_size;
    metadata[0].offset = curr_offset;
    metadata[0].dir = 0;
    metadata[0].root = 1;
    metadata[0].permissions = st.st_mode;
    strcpy(metadata[0].parent_folder, "");
    strcpy(metadata[0].parent_folder_trim, "");

    // update offset to reflect new file
    curr_offset += sizeof(st.st_size);

    header.meta_offset = curr_offset;                  // is curr_offset viable right now

    // read in .Z file
    fd = open(filename, O_RDONLY);
    byte * input = _new(byte, st.st_size);
    read(fd, input, st.st_size);
    _setsize(input, st.st_size);
    close(fd);

    // write out data to the .pitt
    fp = fopen(pitt_file, "a+");
    fwrite(input, _len(input), 1, fp);
    fclose(fp);

    // delete the input that was read in
    _del(input);
    free(properName);
}



/*      Recursively traverses a directory and combines
**      it's .Z files in to a single .pitt file
*/
void archive(char * directory, char * pitt_file)
{
    /* Declarations */
    DIR * curr;
    struct dirent * dir_rdr;
    char * newname;
    struct stat st;
    FILE * fp;
    int fd;

    /* Open directory for reading */
    if ((curr = opendir(directory)) == (void *)0)
    {
        fprintf(stderr, "This is not a directory: %s\n", directory);
    }

    // Get stat information for current directory
    stat(directory, &st);

    /* fill in relavent inode information for this directory */
    strcpy(metadata[metanum].name, directory);                         // relative path to dir
    strcpy(metadata[metanum].name_trim, trimmer(directory));           // physical name of dir
    metadata[metanum].size = -1;
    metadata[metanum].offset = -1;
    metadata[metanum].dir = 1;
    metadata[metanum].permissions = st.st_mode;
    strcpy(metadata[metanum].parent_folder, last_dir);         // This is the full relative path to parent
    strcpy(metadata[metanum].parent_folder_trim, trimmer(last_dir));   // This is the physical name of parent
    ++metanum;

    while ((dir_rdr = readdir(curr)) != (void *)0)
    {
        // Ignore certain types of files
        if (dir_rdr->d_ino == 0) continue;
        if (dir_rdr->d_name[0] == '.') continue;
        if (strcmp(dir_rdr->d_name,".") == 0) continue;
        if (strcmp(dir_rdr->d_name, "..") == 0) continue;

        // Build the relational path to this element
        newname = (char *) malloc(strlen(directory) + strlen(dir_rdr->d_name)+2);
        strcpy(newname, directory);
        strcat(newname, "/");
        strcat(newname, dir_rdr->d_name);

        // Get stat information for current in to st
        stat(newname, &st);

        // if dir element is a folder recursively handle it
        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {
            strcpy(last_dir, directory);
            archive(newname, pitt_file);
        }
        // else if its a file
        else if ((st.st_mode & S_IFMT) == S_IFREG)
        {
            // only care if its a .Z file
            int len = strlen(dir_rdr->d_name);
            if ((dir_rdr->d_name[len-1] == 'Z') && (dir_rdr->d_name[len-2] == '.'))
            {
                printf ("Archiving: %s\n", dir_rdr->d_name);
                // fill in relavent inode information
                strcpy(metadata[metanum].name, newname);
                strcpy(metadata[metanum].name_trim, trimmer(newname));
                metadata[metanum].size = (int) st.st_size;
                metadata[metanum].offset = curr_offset;
                metadata[metanum].dir = 0;
                metadata[metanum].permissions = st.st_mode;
                // This is the full relative path of the parent folder
                strcpy(metadata[metanum].parent_folder, directory);
                // This is the physical name of the parent folder
                strcpy(metadata[metanum].parent_folder_trim, trimmer(directory));
                // Update metadata iterator
                ++metanum;

                // Read in the data
                fd = open(newname, O_RDONLY);
                byte * input = _new(byte, st.st_size);
                read(fd, input, st.st_size);
                _setsize(input, st.st_size);
                close(fd);


                // write out data
                fp = fopen(pitt_file, "a+");
                fwrite(input, _len(input), 1, fp);
                fclose(fp);

                // delete the input that was read in
                _del(input);

                // Update where we are in the .pitt file
                curr_offset += (double) st.st_size;
            }
        }
        free(newname);
    }
    closedir(curr);
}


/*      Recursively traverses a directory, creating .Z files along the way.
**
*/
void traverse(char * curr_directory, int depth)
{
    /* Declarations */
    DIR * curr;
    struct dirent * dir_rdr;
    char * newname;
    struct stat st;
    int i=0;
    int len=0;

    /* Open directory */
    if ((curr = opendir(curr_directory)) == (void *)0)
    {
        fprintf(stderr, "This is not a directory: %s\n", curr_directory);
        perror("Error: ");
        exit(1);
    }
    // For each elmt in the dir
    while ((dir_rdr = readdir(curr)) != (void *)0)
    {
        // Ignore certain elements
        if (dir_rdr->d_ino == 0) continue;
        if (strcmp(dir_rdr->d_name,".") == 0) continue;
        if (strcmp(dir_rdr->d_name, "..") == 0) continue;
        if (dir_rdr->d_name[0] == '.') continue;

        // Create the relational name of the current element
        newname = (char *) malloc(strlen(curr_directory) + strlen(dir_rdr->d_name)+2);
        strcpy(newname, curr_directory);
        strcat(newname, "/");
        strcat(newname, dir_rdr->d_name);

        // Get stat information in st
        stat(newname, &st);

        // If its a directory, recursively try it
        if ((st.st_mode & S_IFMT) == S_IFDIR)
        {
            ++count;
            traverse(newname, depth+1);
        }

        // If its a file, print it then compress it
        else if ((st.st_mode & S_IFMT) == S_IFREG)
        {
            len = strlen(dir_rdr->d_name);
            if (!((dir_rdr->d_name[len-1] == 'Z') && (dir_rdr->d_name[len-2] == '.')))
            {
                count++;
                if (fork() == 0)
                {
                    printf ("Compressing: %s\n", newname);
                    execl("./compress", "compress", newname, NULL);
                    fprintf(stderr, "Make sure executable \"compress\" is in the presentn working directory\n");
                    perror("Could not exec: \n");
                }
            }
        }
        free(newname);
    }
    if(closedir(curr) == -1)
    {
        perror("Error closing directory: ");
    }
}

/*      prints meta-data information for each file in the pitt_file
**
*/
void printMeta(char * pitt_file)
{
    // Declarations
    header h;
    meta * m;
    int i;

    FILE *fp = fopen(pitt_file, "r");
    fread(&h, sizeof(header), 1, fp);

    printf("=======================================================\n");
    printf("=======================================================\n");

    m = (meta *) malloc(sizeof(meta) * h.num_elts);

    fseek(fp, h.meta_offset, SEEK_SET);
    fread(m, sizeof(meta), h.num_elts, fp);
    fclose(fp);

    for (i = 0; i < h.num_elts; ++i)
    {
        printf ("meta name: %s\n", m[i].name);
        printf ("meta name trim: %s\n", m[i].name_trim);
        printf ("meta size: %d\n", m[i].size);
        printf ("meta offset: %d\n", m[i].offset);
        printf ("meta dir: %d\n", m[i].dir);
        printf ("meta root: %d\n", m[i].root);
        printf ("meta permissions: %d\n", m[i].permissions);
        printf ("meta parent: %s\n", m[i].parent_folder);
        printf ("meta parent_folder: %s\n", m[i].parent_folder_trim);
        printf("=======================================================\n");
    }
    while (h.next != -1)
    {
        fp = fopen(pitt_file, "r");
        fseek(fp, h.next, SEEK_SET);
        fread(&h, sizeof(header), 1, fp);
        printf("=======================================================\n");
        printf("=======================================================\n");

        m = (meta *) realloc(m, sizeof(meta) * h.num_elts);

        fseek(fp, h.meta_offset, SEEK_SET);
        fread(m, sizeof(meta), h.num_elts, fp);
        fclose(fp);

        for (i = 0; i < h.num_elts; ++i)
        {
            printf ("meta name: %s\n", m[i].name);
            printf ("meta name trim: %s\n", m[i].name_trim);
            printf ("meta size: %d\n", m[i].size);
            printf ("meta offset: %d\n", m[i].offset);
            printf ("meta dir: %d\n", m[i].dir);
            printf ("meta root: %d\n", m[i].root);
            printf ("meta permissions: %d\n", m[i].permissions);
            printf ("meta parent: %s\n", m[i].parent_folder);
            printf ("meta parent_folder: %s\n", m[i].parent_folder_trim);
            printf("=======================================================\n");
        }
    }
    free(m);
}


// Assume root has already been printed
void rPrintHierarchy(meta * m, int num_elts, char * rootname, int depth)
{
    int i, j;

    for (i = 0; i < num_elts; ++i)
    {
        // if its not the same as rootfile                // its parent file is rootfile
        if (strcmp(rootname, m[i].name_trim) != 0 && strcmp(m[i].parent_folder_trim, rootname) == 0)
        {
            for (j = 0; j < depth; j++)
            {
                printf ("--");
            }
            printf ("%s\n", zTrim(m[i].name_trim));
            if (m[i].dir)
            {
                rPrintHierarchy(m, num_elts, m[i].name_trim, depth+1);
            }
        }
    }
}

/* Prints the file hierarchy of each root file */
void printHierarchy(char * pitt_file)
{
    // Declarations
    header h;
    int i;
    meta * m;
    char rootname[200];
    char parent_folder[200];
    FILE *fp = fopen(pitt_file, "r");
    if (fp == NULL)
    {
        perror("Error opening pitt file: ");
        exit(1);
    }

    fread(&h, sizeof(header), 1, fp);
    fseek(fp, h.meta_offset, SEEK_SET);

    m = (meta *) malloc(sizeof(meta) * h.num_elts);
    fread(m, sizeof(meta), h.num_elts, fp);

    strcpy(rootname, m[0].name_trim);
    strcpy(parent_folder, m[0].parent_folder_trim);

    printf ("%s\n", rootname);
    rPrintHierarchy(m, h.num_elts, rootname, 1);

    while (h.next != -1)
    {
        fseek(fp, h.next, SEEK_SET);
        fread(&h, sizeof(header), 1, fp);
        fseek(fp, h.meta_offset, SEEK_SET);

        m = (meta *) realloc(m, sizeof(meta) * h.num_elts);
        fread(m, sizeof(meta), h.num_elts, fp);

        strcpy(rootname, m[0].name_trim);
        strcpy(parent_folder, m[0].parent_folder_trim);

        printf ("===============\n");
        printf ("%s\n", rootname);
        rPrintHierarchy(m, h.num_elts, rootname, 1);
    }

    fclose(fp);
    free(m);
}

/*      Extract a file from a portion of a .pitt file
**
*/
void extractfile(int offset, int size, int permissions, char * path, FILE * fp)
{
    byte * in;
    byte * out;
    int fd;

    fseek(fp, offset, SEEK_SET);

    printf ("Extracting %s...\n", path);

    in = _new(byte, size);
    fread(in, sizeof(byte), size, fp);
    _setsize(in, size);

    out = lzw_decode(in);

    fd = open(zTrim(path), O_WRONLY | O_CREAT);
    write(fd, out, _len(out));
    fchmod(fd, (mode_t) permissions);
    close(fd);

    _del(out);
    _del(in);
}

/*      Extracts the contents of a metadata array in to a given directory
**      Note:   Starts at 1 because 0 is root and is already created
*/
void extractdir(meta * m, int num_elts, char * relative_path, char * curr_parent, FILE * fp)
{
    // Declarations
    int i;
    char out[300];

    // For each element (aside from root)
    for (i = 1; i < num_elts; ++i)
    {
        // Reset the paths string
        out[0] = 0;

        // if it is a child of the current parent directory
        if (strcmp(m[i].parent_folder_trim, curr_parent) == 0)
        {
            // If its a directory
            if (m[i].dir == 1)
            {
                // Build the relative path
                strcpy(out, relative_path);
                strcat(out, "/");
                strcat(out, m[i].name_trim);
                // Make that directory
                if(mkdir(out, m[i].permissions) == -1)
                {
                    fprintf(stderr, "Error making dir: %s\n", out);
                    free(m);
                    perror("Error: ");
                    exit(1);
                }
                // update permissions
                chmod(out, m[i].permissions);
                // Recursively handle this directory
                extractdir(m, num_elts, out, m[i].name_trim, fp);
            }
            // If its a regular file, extract it
            else
            {
                // Build relative path
                strcpy(out, relative_path);
                strcat(out,"/");
                strcat(out, m[i].name_trim);
                // Extract it
                extractfile(m[i].offset, m[i].size, m[i].permissions, out, fp);
            }
        }
    }
}

/*      Searches for a file/directory then extracts it
**              Gonna want to use the extractfile and extractdir subroutines
*/
void extract_specific(char * element, char * pitt_file)
{
    header h;
    int i;
    meta * m = malloc(sizeof(meta));
    FILE *fp = fopen(pitt_file, "r");
    char curr_name[100];
    char path[100];    

    do
    {
        fread (&h, sizeof(header), 1, fp);
        fseek (fp, h.meta_offset, SEEK_SET);
        m = (meta *) realloc(m, sizeof(meta) * h.num_elts);
        fread (m, sizeof(meta), h.num_elts, fp);
	fseek (fp, h.next, SEEK_SET);
        for (i = 0; i < h.num_elts; i++)
        {
            path[0] = 0;
            curr_name[0] = 0;
	    //printf ("~~~~~%s", m[i].name);
            strcpy(curr_name, zTrim(trimmer(m[i].name)));
            if (strcmp(element, curr_name) == 0)
            {
                if (m[i].dir == 1)
                {
                    strcpy(path, "./");
                    strcat(path, m[i].name_trim);
                    if(mkdir(path, m[0].permissions) == -1)
		    {
			fprintf(stderr, "Error making dir: %s\n", m[0].name_trim);
			free(m);

			perror("Exiting gracefully from : ");
			exit(1);
		    }
		    chmod(path, m[0].permissions);
		    strcpy(path, m[0].name_trim);
		    // Build its children via the extractdir function
		    extractdir(m, h.num_elts, path, m[0].parent_folder_trim, fp);
		    goto gtfo;
                }
                else
                {
                    strcpy(path, "./");
                    strcat(path, zTrim(m[i].name_trim));
                    extractfile(m[i].offset, m[i].size, m[i].permissions, path, fp);
		    goto gtfo;
                }
            }
        }
	if (h.next == -1)
	{
	    goto gtfo;
	}

    } while (1);
gtfo:
    free(m);
}


/*      Decompresses a .pitt file
**      Note the first element in the meta array is always the root
*/
void extract(char * pitt_file)
{
    // Declarations
    header h;
    meta * m;
    char path[300];
    FILE *fp = fopen(pitt_file, "r");
    m = (meta *) malloc(sizeof(meta));                      // Initial meta so realloc works maybe?
    do
    {
        path[0] = 0;                                        // Reset path variable
        // Get metadata for this compresssion
        fread(&h, sizeof(header), 1, fp);                   // Read in the header
        fseek(fp, h.meta_offset, SEEK_SET);                 // Seek to the metadata
        m = realloc(m, sizeof(meta) * h.num_elts);          // allocate enough space for the metadata array
        fread(m, sizeof(meta), h.num_elts, fp);             // read in the metadata array
        // If the root is a directory
        if (m[0].dir == 1)
        {
            // Make the directory
            if(mkdir(m[0].name_trim, m[0].permissions) == -1)
            {
                fprintf(stderr, "Error making dir: %s\n", m[0].name_trim);
                free(m);

                perror("Exiting gracefully from : ");
                exit(1);
            }
            chmod(m[0].name_trim, m[0].permissions);
            strcpy(path, m[0].name_trim);
            // Build its children via the extractdir function
            extractdir(m, h.num_elts, path, m[0].parent_folder_trim, fp);
        }
        // if the root is a file
        else
        {
            // Just extract it to the current directory
            extractfile(m[0].offset, m[0].size, m[0].permissions,"", fp);
        }
        // If there is another compressed file
        if (h.next > 0)
        {
            // seek to its header
            fseek(fp, h.next, SEEK_SET);
        }
        // Loop if there is another compressed file
    } while (h.next > 0);
    // Free the metadata array
    free(m);
}


/*      Go through the .pitt file and offset the elements of
**
*/
void update_meta(char * pitt_file, int size)
{
    header h;
    int i;
    int old_offset, old_next;
    meta * m;
    char path[300];
    FILE *fp = fopen(pitt_file, "r+");
    m = (meta *) malloc(sizeof(meta));
    do
    {
        // Update current header
        fread (&h, sizeof(header), 1, fp);
        old_offset = h.meta_offset;
        h.meta_offset += size;
        old_next = h.next;
        if (h.next > 0)
        {
            h.next += size;
        }
        fseek (fp, (-1)*(sizeof(header)), SEEK_CUR);
        fwrite (&h, sizeof(header), 1, fp);

        // update header's metadata
        fseek (fp, old_offset, SEEK_SET);
        m = realloc(m, sizeof(meta) * h.num_elts);
        fread (m, sizeof(meta), h.num_elts, fp);
        for (i = 0; i < h.num_elts; ++i)
        {
            m[i].offset += size;
        }
        fseek (fp, old_offset, SEEK_SET);
        fwrite (m, sizeof(meta), h.num_elts, fp);\

        // get next header
        fseek (fp, old_next, SEEK_SET);

    } while (old_next > 0);

    // clean up
    fclose(fp);
    free(m);
}

/*      Compress a file/dir and prepend it to a .pitt
**
*/
void append(char * element, char * pitt_file)
{
    // Declarations
    header h;
    struct stat st;
    int fd;
    char * new_file = "tmp.pitt";                 // temporary file that will be prepended to the .pitt

    // Reset global variables
    last_dir[0] = 0;
    count = 0;
    metanum = 0;
    curr_offset = 0;


    // Compress the element to be prepended
    compress(element, new_file);
    stat(new_file, &st);

    // Update newly compressed files header to point to the .pitt's header
    FILE *fp = fopen(new_file, "r+");             // Open the newly compressed file
    fread(&h, sizeof(header), 1, fp);             // Read in its header

    fseek(fp, 0, SEEK_END);
    h.next = ftell(fp);                           // the begnning of .pitt will be at the end of this file
    rewind(fp);
    fwrite(&h, sizeof(header), 1, fp);            // write the updated header
    fclose(fp);

    // Update every header in the .pitt file to reflect the appended size
    update_meta(pitt_file, (int) st.st_size);


    stat(pitt_file, &st);

    // Read in the pitt file
    byte * input = (byte *) malloc(st.st_size);
    fp = fopen(pitt_file, "r");
    int x = fread(input, st.st_size, 1, fp);
    fclose(fp);

    // Write it to the end of the tmp file
    fp = fopen(new_file, "r+");
    fseek(fp, 0, SEEK_END);
    x = fwrite(input, st.st_size, 1, fp);
    fclose(fp);

    // Renamet the temp file to the .pitt file
    if (rename(new_file, pitt_file) != 0)
        perror("Error renaming .pitt.pitt");


    free(input);
}

/* compresses a file/dir and creates a .pitt for it */
void compress(char * element, char * pitt_file)
{
    // Declarations
    FILE * fp;
    struct stat st;
    header header;
    stat(element, &st);
    int status;
    int i;

    // If its a directory
    if ((st.st_mode & S_IFMT) == S_IFDIR)
    {
        // increment count by one to reflect the initial folder
        count = 1;
        // traverse it and make .Z files along the way
        traverse(element, 0);

        // Malloc the amount of meta data that will be neeed
        metadata = (meta *) malloc(sizeof(meta) * count);

        // update the offset for header info
        curr_offset += sizeof(header);                                // count

        // Header = number of elements that are to be compressed
        header.meta_offset = curr_offset;
        header.next = -1;
        header.num_elts = count;

        /* Create the .pitt file and it's header. */
        fp = fopen(pitt_file, "a+");
        fwrite(&header, sizeof(header), 1, fp);
        fclose(fp);

        // wait for compressions to finish
        wait(&status);

        // append .Z files to the .pitt file
        strcpy(last_dir, element);
        archive(element, pitt_file);

        // Specify this directory as a root
        for (i = 0; i < count; ++i)
        {
            if (strcmp(metadata[i].name, element)==0)
            {
                metadata[i].root = 1;
                break;
            }
        }

        // update where the metadata will be
        fp = fopen(pitt_file, "r+");
        fseek(fp, 0, SEEK_END);
        curr_offset = ftell(fp);
        rewind(fp);
        fread(&header, sizeof(header), 1, fp);
        header.meta_offset = curr_offset;
        rewind(fp);
        fwrite(&header, sizeof(header), 1, fp);
        fclose(fp);

        // Write the metadata to the end of the .pitt file
        append_meta(pitt_file);

        free(metadata);
    }
    // If its a file
    else if ((st.st_mode & S_IFMT) == S_IFREG)
    {
        char * newname = (char *) malloc(strlen(element) + 3);
        // Compress it to a .Z
        if (fork() == 0)
        {
            execl("./compress", "compress", element, NULL);
            perror("Error execing compress: ");
        }

        // create metadata array for it (size one)
        metadata = (meta *) malloc(sizeof(meta));

        // build the name of .Z file
        strcpy(newname, element);
        strcat(newname, ".Z");

        wait(&status);

        // archive it in to a .pitt
        count = 1;                            // There will only be one file in the .pitt
        archive_file(newname, pitt_file);

        append_meta(pitt_file);

        free(metadata);
        free(newname);
    }
}

void usage()
{
    printf ("Usage:\n");
    printf("\n\tpittar {-c|-a|-x|-m|-p} <archive-file> <file/directory list>\n\n");
}

int main(int argc, char *argv[])
{
    int opt, cflag, aflag, xflag, pflag, mflag;
    char * pitt_file;

    if (argc == 1)
    {
        fprintf(stderr, "Please specify arguments.\n");
        usage();
        return 1;
    }
    while ((opt = getopt(argc, argv, "jc:a:x:p:m:")) != -1)
    {
        switch (opt)
        {
        case 'c': {cflag = 1; pitt_file = optarg; break;}
        case 'a': {aflag = 1; pitt_file = optarg; break;}
        case 'x': {xflag = 1; pitt_file = optarg; break;}
        case 'p': {pflag = 1; pitt_file = optarg; break;}
        case 'm': {mflag = 1; pitt_file = optarg; break;}
        }
    }
    if (cflag != 1 && aflag != 1 && xflag != 1 && pflag != 1 && mflag != 1)
    {
        usage();
    }
    else if (cflag == 1)
    {
        // compress argv[optind,...] into pitt_file+".pitt"
        char * out = (char *) malloc(strlen(pitt_file) + strlen(".pitt"));
        if (pitt_file == NULL)
        {
            fprintf(stderr, "Please specify archive file.\n");
            usage();
            return 1;
        }
        strcpy(out, pitt_file);
        if (argv[optind] == NULL)
        {
            fprintf(stderr, "No file to compress specified!\n: ");
            usage();
            exit(1);
        }
        compress(argv[optind], strcat(out, ".pitt"));

        ++optind;
        while (optind<argc)
        {
            printf ("Appending %s to %s\n", argv[optind], out);
            append(argv[optind], out);
            ++optind;
        }
        free(out);
    }
    else if (aflag == 1)
    {
        if (pitt_file == NULL)
        {
            fprintf(stderr, "Please specify archive file.\n");
            usage();
            return 1;
        }
        if (argv[optind] == NULL)
        {
            fprintf(stderr, "No file to compress specified!\n: ");
            usage();
            exit(1);
        }
        while (optind<argc)
        {
            printf ("Appending %s to %s\n", argv[optind], pitt_file);
            append(argv[optind], pitt_file);
            ++optind;
        }
    }
    else if (xflag == 1)
    {
        if (pitt_file == NULL)
        {
            fprintf(stderr, "Please specify archive file.\n");
            usage();
            return 1;
        }
        if (argc == 3)
        {
            extract(pitt_file);
        }
        else
        {
            while (optind<argc)
            {
                extract_specific(argv[optind], pitt_file);
                optind++;
            }
        }
    }
    else if (mflag == 1)
    {
        if (pitt_file == NULL)
        {
            fprintf(stderr, "Please specify archive file.\n");
            usage();
            return 1;
        }
        printHierarchy(pitt_file);
    }
    else if (pflag == 1)
    {
        if (pitt_file == NULL)
        {
            fprintf(stderr, "Please specify archive file.\n");
            usage();
            return 1;
        }
        printMeta(pitt_file);
    }

    return 0;
}
