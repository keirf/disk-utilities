/**
 * @brief Dump information about an IMD image file.
 * @author David Knoll
 * @file
 */

#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <limits.h>

#define F_C 1
#define F_T 2
#define F_S 4
#define F_E 8
#define RDSZ(i) ((map_sz[(i << 1) | 1] << 8) | map_sz[i << 1])

int main(int argc, char *argv[])
{
    long total_sz = 0;
    unsigned char opts = 0;
    int cnt_sec = 0, cnt_comp = 0, cnt_delerr = 0;
    int i, j, nsec, base_id;
    FILE *imdfile;
    unsigned char trkinfo[5], map_num[256], map_cyl[256], map_hd[256], map_sz[512];

    // Parse command line options
    while ((i = getopt(argc, argv, "ctse")) != -1) {
        switch (i) {
        case 'c':
            opts |= F_C;
            break;
        case 't':
            opts |= F_T;
            break;
        case 's':
            opts |= F_S;
            break;
        case 'e':
            opts |= F_E;
            break;

        default:
            fprintf(stderr, "Usage: %s [-c] [-t] [-s] [-e] imdfile\n\
  -c  Output the IMD file header / comment\n\
  -t  Output per-track information\n\
  -s  Output per-sector information\n\
  -e  Output a few stats at the end\n", argv[0]);
            return 1;
        }
    }

    // Open IMD file for reading
    imdfile = fopen(argv[argc - 1], "rb");
    if (!imdfile) {
        fprintf(stderr, "Error opening file: %s\n", argv[argc - 1]);
        return 2;
    }

    // Output IMD header / comment
    while (1) {
        i = fgetc(imdfile);
        if (i == 0x1A) { break; }
        if (i < 0) {
            fprintf(stderr, "Error reading file\n");
            fclose(imdfile);
            return 3;
        }
        if (opts & F_C) { putchar(i); }
    }

    // For every track
    while (1) {
        // Output basic track information
        if (fread(trkinfo, 1, 5, imdfile) != 5)
            break;
        nsec = trkinfo[3];
        if (opts & F_T) {
            printf("\nCylinder %2d Head %2d\n", trkinfo[1], trkinfo[2] & 0xF);

            switch (trkinfo[0]) {
            case 0:
                printf("  500 kbps FM\n");
                break;
            case 1:
                printf("  300 kbps FM\n");
                break;
            case 2:
                printf("  250 kbps FM\n");
                break;
            case 3:
                printf("  500 kbps MFM\n");
                break;
            case 4:
                printf("  300 kbps MFM\n");
                break;
            case 5:
                printf("  250 kbps MFM\n");
                break;
            default:
                printf("  Unknown density\n");
            }

            if (trkinfo[4] == 0xFF) {
                printf("  %d sectors of variable size\n", nsec);
            } else {
                printf("  %d sectors of %d bytes/sector\n", nsec, 128 << trkinfo[4]);
            }
        }

        // Load numbering/cylinder/head maps
        if (fread(map_num, 1, nsec, imdfile) != nsec)
            err(1, NULL);
        base_id = INT_MAX;
        for (i = 0; i < nsec; i++)
            if (map_num[i] < base_id)
                base_id = map_num[i];
        for (i = 0; i < nsec && map_num[i] != base_id; i++)
            continue;
        for (j = 0; j < nsec && map_num[j] != base_id+1; j++)
            continue;
        if (opts & F_T) { printf("  %d:1 interleave\n", (nsec + j - i) % nsec); }

        if (trkinfo[2] & 0x80) {
            if (fread(map_cyl, 1, nsec, imdfile) != nsec)
                err(1, NULL);
            if (opts & F_T) { printf("  Physical and recorded cylinder numbers may not match\n"); }
        }
        if (trkinfo[2] & 0x40) {
            if (fread(map_hd, 1, nsec, imdfile) != nsec)
                err(1, NULL);
            if (opts & F_T) { printf("  Physical and recorded head numbers may not match\n"); }
        }

        if (trkinfo[4] == 0xFF) {
            if (fread(map_sz, 2, nsec, imdfile) != nsec)
                err(1, NULL);
        }

        // For every sector on this track
        for (i = 0; i < nsec; i++) {
            if (opts & F_S) { printf("  Sector %2d\n", i + 1); }
            if (trkinfo[4] == 0xFF) {
                total_sz += RDSZ(i);
            } else {
                total_sz += (128 << trkinfo[4]);
            }
            cnt_sec++;

            // Check data record type
            j = fgetc(imdfile);
            if (j == 0) {
                if (opts & F_S) { printf("    Data unavailable\n"); }
                cnt_delerr++;

            } else if (j <= 8) {
                // Data record types containing some known data
                if (opts & F_S) {
                    printf("    %sompressed / %seleted / %srror\n",
                           ((j - 1) & 1) ? "C" : "Not c",
                           ((j - 1) & 2) ? "D" : "Not d",
                           ((j - 1) & 4) ? "E" : "No e"
                        );
                }

                if ((j - 1) & 1) {
                    // Skip over compressed data
                    fseek(imdfile, 1, SEEK_CUR);
                    cnt_comp++;
                } else {
                    // Skip over data
                    fseek(imdfile, 128 << trkinfo[4], SEEK_CUR);
                }
                if ((j - 1) & 6) { cnt_delerr++; }

            } else {
                if (opts & F_S) { printf("    Unknown data record\n"); }
                cnt_delerr++;
            }
        }
    }

    // Print summary stats and exit
    if (opts & F_E) {
        printf("\n%d total sectors / %ld KiB\n", cnt_sec, total_sz >> 10);
        printf("  %d sectors (%d%%) stored compressed\n", cnt_comp, (cnt_comp * 100) / cnt_sec);
        printf("  %d sectors (%d%%) bad/deleted/with errors\n", cnt_delerr, (cnt_delerr * 100) / cnt_sec);
    }
    fclose(imdfile);
    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
