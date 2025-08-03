#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <fnmatch.h>

#include "race-file.h"

#include "objects.h"
#include "macros.h"

#ifdef _WITH_SS5
#include "constants.ss5.h"
#else
#include "constants.h"
#endif

#include "stats.h"
#include "zsfunctions.h"
#include "helpfunctions.h"

#ifndef _CRC_H_
#include "crc.h"
#endif

#include "../conf/zsconfig.h"
#include "../include/zsconfig.defaults.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_STRLCPY
# include "strl/strl.h"
#endif


/*
 * Modified	: 02.19.2002 Author	: Dark0n3
 * Modified	: 06.02.2005 by		: js
 * Description	: Creates directory where all race information will be
 * stored.
 */
void
maketempdir(char *path)
{
	char		full_path[PATH_MAX], *p;

	snprintf(full_path, PATH_MAX, "%s/%s", storage, path);

	/* work recursively */
	for (p = full_path; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (strlen(full_path) && mkdir(full_path, 0777) == -1 && errno != EEXIST)
				d_log("maketempdir: Failed to create tempdir (%s): %s\n", full_path, strerror(errno));
			*p = '/';
		}
	}

	/* the final entry */
	if (mkdir(full_path, 0777) == -1 && errno != EEXIST)
		d_log("maketempdir: Failed to create tempdir (%s): %s\n", full_path, strerror(errno));
}

/*
 * Modified	: 2002.01.16	Author	: Dark0n3
 * Modified	: 2011.08.10	by	: Sked
 * Description	: Reads crc for current file from preparsed sfv file.
 */
unsigned int
readsfv(const char *path, struct VARS *raceI, int getfcount)
{
	unsigned int	crc = 0;
	FILE		*sfvfile;
	DIR		*dir;

	SFVDATA		sd;

	if (!(sfvfile = fopen(path, "r"))) {
		d_log("readsfv: Failed to open sfv (%s): %s\n", path, strerror(errno));
		return 0;
	}

	if (!update_lock(raceI, 1, 0)) {
		d_log("readsfv: Lock is suggested removed. Will comply and exit\n");
		fclose(sfvfile);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	raceI->misc.release_type = raceI->data_type;

	d_log("readsfv: Reading data from sfv for (%s)\n", raceI->file.name);

	dir = opendir(".");

	raceI->total.files = 0;

	while (fread(&sd, sizeof(SFVDATA), 1, sfvfile)) {
		raceI->total.files++;

		if (lenient_compare(raceI->file.name, sd.fname)) {
			d_log("readsfv: crc read from sfv-file (%s): %.8x\n", sd.fname, (unsigned int)sd.crc32);
			crc = (unsigned int)sd.crc32;
			strncpy(raceI->file.unlink, sd.fname, sizeof(raceI->file.unlink));
		}

		if (getfcount && findfile(dir, sd.fname))
			raceI->total.files_missing--;
	}

	closedir(dir);
	fclose(sfvfile);

	raceI->total.files_missing += raceI->total.files;

	if (raceI->total.files_missing < 0) {
		d_log("readsfv: GAKK! raceI->total.files_missing %d < 0\n", raceI->total.files_missing);
		raceI->total.files_missing = 0;
	}

	return crc;
}

/*
 * First Version: 2013.07.15	by	: Sked
 * Description	: Gets the first filename from the sfvdata and returns it. This should be freed later.
 */
char *
get_first_filename_from_sfvdata(const char *sfvdatafile)
{
	char	*firstfile;
	FILE	*sfvfile;
	SFVDATA	sd;

	if (!(sfvfile = fopen(sfvdatafile, "r"))) {
		d_log("readsfv: Failed to open sfv (%s): %s\n", sfvdatafile, strerror(errno));
		return 0;
	}

	fread(&sd, sizeof(SFVDATA), 1, sfvfile);
	fclose(sfvfile);

	firstfile = ng_realloc2(firstfile, (strlen(sd.fname) + 1), 0, 1, 1);
	strcpy(firstfile, sd.fname);

	return firstfile;
}

void
update_sfvdata(const char *path, const char *fname, const unsigned int crc)
{
	int		fd, count;

	SFVDATA		sd;

	if ((fd = open(path, O_RDWR, 0666)) == -1) {
		d_log("update_sfvdata: Failed to open sfvdata (%s): %s\n", path, strerror(errno));
		return;
	}

	sd.crc32 = crc;
	count = 0;
	while (read(fd, &sd, sizeof(SFVDATA))) {
		if (!strcasecmp(fname, sd.fname)) {
			sd.crc32 = crc;
			break;
		}
		count++;
	}

	lseek(fd, sizeof(SFVDATA) * count, SEEK_SET);
	if (write(fd, &sd, sizeof(SFVDATA)) != sizeof(SFVDATA))
		d_log("update_sfvdata: write failed: %s\n", strerror(errno));
	close(fd);
}

/*
 * Modified	: 01.16.2002 Author	: Dark0n3
 *
 * Description	: Deletes all -missing files with preparsed sfv.
 */
void
delete_sfv(const char *path, struct VARS *raceI)
{
	char		*f = 0, missing_fname[NAME_MAX];
	FILE		*sfvfile;

	SFVDATA		sd;

	if (!(sfvfile = fopen(path, "r"))) {
		d_log("delete_sfv: Couldn't fopen %s: %s\n", path, strerror(errno));
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	while (fread(&sd, sizeof(SFVDATA), 1, sfvfile)) {
		snprintf(missing_fname, NAME_MAX, "%s-missing", sd.fname);
		if ((f = findfilename(missing_fname, f, raceI)))
                {
			if (unlink(missing_fname) < 0)
                            d_log("delete_sfv: Couldn't unlink missing-indicator '%s': %s\n", missing_fname, strerror(errno));
                }
	}
	ng_free(f);
	fclose(sfvfile);
}

/*
 * Modified	: 01.16.2002 Author	: Dark0n3
 *
 * Description	: Reads name of old race leader and writes name of new leader
 * 		  into temporary file.
 *
 */
void
read_write_leader(const char *path, struct VARS *raceI, struct USERINFO *userI)
{
	int		fd;
	struct stat	sb;

	if ((fd = open(path, O_CREAT | O_RDWR, 0666)) == -1) {
		d_log("read_write_leader: open(%s): %s\n", path, strerror(errno));
		return;
	}

	if (!update_lock(raceI, 1, 0)) {
		d_log("read_write_leader: Lock is suggested removed. Will comply and exit\n");
		close(fd);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	fstat(fd, &sb);

	if (sb.st_size == 0) {
		*raceI->misc.old_leader = '\0';
	} else {
		if (read(fd, &raceI->misc.old_leader, 24) == -1) {
			d_log("read_write_leader: read() failed: %s\n", strerror(errno));
		}
		lseek(fd, 0L, SEEK_SET);
	}

	if (write(fd, userI->name, 24) != 24)
		d_log("read_write_leader: write failed: %s\n", strerror(errno));

	close(fd);
}

/*
 * Modified	: 01.16.2002 Author	: Dark0n3
 *
 * Description	: Goes through all untested files and compares crc of file
 * with one that is reported in sfv.
 *
 */
void
testfiles(struct LOCATIONS *locations, struct VARS *raceI, int rstatus)
{
	int		fd, lret, count;
	char		*ext, target[PATH_MAX], real_file[PATH_MAX];
	FILE		*racefile;
	unsigned int	Tcrc;
	struct stat	filestat;
	time_t		timenow;
	RACEDATA	rd;

	/* create if it doesn't exist yet and don't truncate if it does */
	if ((fd = open(locations->race, O_CREAT | O_RDWR, 0666)) == -1) {
		if (errno != EEXIST) {
			d_log("testfiles: open(%s): %s\n", locations->race, strerror(errno));
			remove_lock(raceI);
			exit(EXIT_FAILURE);
		}
	}
	close(fd);

	if (!(racefile = fopen(locations->race, "r+"))) {
		d_log("testfiles: fopen(%s) failed\n", locations->race);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	strlcpy(real_file, raceI->file.name, sizeof(real_file));

	if (rstatus)
		printf("\n");

	count = 0;
	rd.status = F_NOTCHECKED;
	while ((fread(&rd, sizeof(RACEDATA), 1, racefile))) {
		if (!update_lock(raceI, 1, 0)) {
			d_log("testfiles: Lock is suggested removed. Will comply and exit\n");
			fclose(racefile);
			remove_lock(raceI);
			exit(EXIT_FAILURE);
		}
		ext = find_last_of(rd.fname, ".");
		if (*ext == '.')
			ext++;
		strlcpy(raceI->file.name, rd.fname, NAME_MAX);
		Tcrc = readsfv(locations->sfv, raceI, 0);
		timenow = time(NULL);
		stat(rd.fname, &filestat);
		if (fileexists(rd.fname)) {
			d_log("testfiles: Processing %s\n", rd.fname);
			if (S_ISDIR(filestat.st_mode))
				rd.status = F_IGNORED;
			else if (rd.crc32 != 0 && Tcrc == rd.crc32)
				rd.status = F_CHECKED;
			else if (rd.crc32 != 0 && strcomp(ignored_types, ext))
				rd.status = F_IGNORED;
			else if (rd.crc32 != 0 && Tcrc == 0 && (strcomp(allowed_types, ext) && !matchpath(allowed_types_exemption_dirs, locations->path)))
				rd.status = F_IGNORED;
			else if ((rd.crc32 != 0) && (Tcrc != rd.crc32) &&
					  (strcomp(allowed_types, ext) &&
					   !matchpath(allowed_types_exemption_dirs, locations->path)))
				rd.status = F_IGNORED;
			else if ((rd.crc32 == 0) && strcomp(allowed_types, ext))
				rd.status = F_IGNORED;
			else if ((timenow == filestat.st_ctime) && (filestat.st_mode & 0111)) {
				d_log("testfiles: Looks like this file (%s) is in the process of being uploaded. Ignoring.\n", rd.fname);
				rd.status = F_IGNORED;
				create_missing(rd.fname);
			}
		} else if (snprintf(target, sizeof(target), "%s.bad", rd.fname) > 4 && fileexists(target)) {
       	                d_log("testfiles: File doesnt exist (%s), bad version of it does, keeping it marked as bad.\n", rd.fname);
			rd.status = F_BAD;
			if (rstatus)
				printf("File: %s BAD!\n", rd.fname);
		} else {
                        d_log("testfiles: File doesnt exist (%s), marking as missing.\n", rd.fname);
			rd.status = F_MISSING;
			if (rstatus)
				printf("File: %s MISSING!\n", rd.fname);
			remove_from_race(locations->race, rd.fname, raceI);
			--count;
		}

		if (rd.status == F_MISSING || rd.status == F_NOTCHECKED) {
			if (rd.status == F_NOTCHECKED) {
				d_log("testfiles: Marking file (%s) as bad and removing it.\n", rd.fname);
				mark_as_bad(rd.fname);
				if (rd.fname)
					unlink(rd.fname);
				rd.status = F_BAD;
				if (rstatus)
					printf("File: %s FAILED!\n", rd.fname);

			}
#if ( create_missing_files )
			if (Tcrc != 0)
				create_missing(rd.fname);
#endif

#if (enable_unduper_script == TRUE)
			if (!fileexists(unduper_script)) {
				d_log("Failed to undupe '%s' - '%s' does not exist.\n", rd.fname, unduper_script);
			} else {
				sprintf(target, unduper_script " \"%s\"", rd.fname);
				_err_file_banned(rd.fname, NULL);
				if (execute(target) == 0)
					d_log("testfiles: undupe of %s successful (%s).\n", rd.fname, target);
				else
					d_log("testfiles: undupe of %s failed (%s).\n", rd.fname, target);
			}
#endif
		}

		if (rd.status != F_MISSING) {
			if ((lret = fseek(racefile, sizeof(RACEDATA) * count, SEEK_SET)) == -1) {
				d_log("testfiles: fseek: %s\n", strerror(errno));
				fclose(racefile);
				remove_lock(raceI);
				exit(EXIT_FAILURE);
			}
			if (fwrite(&rd, sizeof(RACEDATA), 1, racefile) == 0)
				d_log("testfiles: write failed: %s\n", strerror(errno));

			if (rd.status != F_BAD && !((timenow == filestat.st_ctime) && (filestat.st_mode & 0111)))
				unlink_missing(rd.fname);
		}
		++count;
	}
	strlcpy(raceI->file.name, real_file, strlen(real_file)+1);
	raceI->total.files = raceI->total.files_missing = 0;
	fclose(racefile);
	d_log("testfiles: finished checking\n");
}

/*
 * Modified	: 01.20.2002 Author	: Dark0n3
 *
 * Description	: Parses file entries from sfv file and store them in a file.
 *
 * Todo		: Add dupefile remover.
 *
 * Totally rewritten by js on 08.02.2005
 */
int
copysfv(const char *source, const char *target, struct VARS *raceI)
{
	int		outfd, i, retval = 0;
	short int	music, rars, video, others, type;

	char		*ptr, fbuf[2048];
	FILE		*insfv;

	DIR		*dir;

	SFVDATA		sd;

//#if ( sfv_dupecheck == TRUE )
	int		skip = 0;
	SFVDATA		tempsd;
//#endif

#if ( sfv_cleanup == TRUE )
	int		tmpfd;
	char		crctmp[16];

	if ((tmpfd = open(".tmpsfv", O_CREAT | O_TRUNC | O_RDWR, 0644)) == -1)
		d_log("copysfv: open(.tmpsfv): %s\n", strerror(errno));
#endif

	if ((insfv = fopen(source, "r")) == NULL) {
		d_log("copysfv: fopen(%s): %s\n", source, strerror(errno));
#if ( sfv_cleanup == TRUE )
		close(tmpfd);
		unlink(".tmpsfv");
#endif
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	if ((outfd = open(target, O_CREAT | O_TRUNC | O_RDWR, 0666)) == -1) {
		d_log("copysfv: open(%s): %s\n", target, strerror(errno));
		fclose(insfv);
#if ( sfv_cleanup == TRUE )
		close(tmpfd);
		unlink(".tmpsfv");
#endif
		unlink(target);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	video = music = rars = others = type = 0;

	dir = opendir(".");

	if (!update_lock(raceI, 1, 0)) {
		d_log("copysfv: Lock is suggested removed. Will comply and exit\n");
		fclose(insfv);
		closedir(dir);
#if ( sfv_cleanup == TRUE )
		close(tmpfd);
		unlink(".tmpsfv");
#endif
		close(outfd);
		unlink(target);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	while ((fgets(fbuf, sizeof(fbuf), insfv))) {

		tailstrip_chars(fbuf, WHITESPACE_STR);
		ptr = prestrip_chars(fbuf, WHITESPACE_STR);
		if (ptr != fbuf)
			d_log("copysfv: prestripped whitespaces (%d chars)\n", ptr - fbuf);

		/* check if ; appears at start of line */
		if ((ptr == find_first_of(ptr, ";"))) {
#if ( sfv_cleanup == TRUE && sfv_cleanup_comments == FALSE )
			/* comments can be written away immediately to .tmpsfv */
			if (write(tmpfd, ptr, strlen(ptr)) != (int)strlen(ptr))
				d_log("copysfv: write of comment failed: %s\n", strerror(errno));
#if (sfv_cleanup_crlf == TRUE )
			if (write(tmpfd, "\r", 1) != 1)
				d_log("copysfv: write of \\r failed: %s\n", strerror(errno));
#endif
			if (write(tmpfd, "\n", 1) != 1)
				d_log("copysfv: write of \\n failed: %s\n", strerror(errno));
#endif
			/* clear comment to prevent further processing */
			*ptr = '\0';
		}

		if (strlen(ptr) == 0)
			continue;

#if (sfv_cleanup_lowercase == TRUE)
		for (; *ptr; ptr++)
			*ptr = tolower(*ptr);
#endif
		sd.crc32 = 0;
		bzero(sd.fname, sizeof(sd.fname));
		if ((ptr = find_last_of(fbuf, " \t"))) {

			/* pass the " \t" */
			ptr++;

			/* what we have now is hopefully a crc */
			for (i = 0; isxdigit(*ptr) != 0; i++)
				ptr++;

			ptr -= i;
			if (i > 8 || i < 6) {
				/* we didn't get an 8 digit crc number */
#if (sfv_cleanup == TRUE)
				/* do stuff  */
				d_log("copysfv: We did not get a 8 digit crc number for %s - trying to continue anyway\n", sd.fname);
#else
				retval = 1;
				goto END;
#endif
			} else {
				sd.crc32 = hexstrtodec(ptr);

				/* cut off crc string */
				*ptr = '\0';

				/* nobody should be stupid enough to have spaces
				 * at the end of the file name */
				tailstrip_chars(fbuf, WHITESPACE_STR);
			}

		} else {
			/* we have a filename only. */
#if (sfv_cleanup == TRUE)
			/* do stuff  */
			d_log("copysfv: We did not find a crc number for %s - trying to continue anyway\n", sd.fname);
#else
			retval = 1;
			goto END;
#endif
		}

		/* we assume what's left is a filename */
		ptr = prestrip_chars(fbuf, WHITESPACE_STR);
		if (ptr != fbuf)
			d_log("copysfv: prestripped whitespaces (%d chars)\n", ptr - fbuf);

#if (allow_slash_in_sfv == TRUE)
		if (ptr != find_last_of(ptr, "/")) {
			ptr = find_last_of(ptr, "/") + 1;
			d_log("copysfv: found '/' in filename - adjusting.\n");
		}
		if (ptr != find_last_of(ptr, "\\")) {
			ptr = find_last_of(ptr, "\\") + 1;
			d_log("copysfv: found '\\' in filename - adjusting.\n");
		}
#endif
		if (strlen(ptr) > 0 && strlen(ptr) < NAME_MAX-9 ) {
			strlcpy(sd.fname, ptr, NAME_MAX-9);

			if (sd.fname != find_last_of(sd.fname, "\t") || sd.fname != find_last_of(sd.fname, "\\") || sd.fname != find_last_of(sd.fname, "/")) {
				d_log("copysfv: found '/', '\\' or <TAB> as part of filename in sfv - logging file as bad.\n");
				retval = 1;
				break;
			}

			if (sd.crc32 == 0) {
#if (sfv_calc_single_fname == TRUE || create_missing_sfv == TRUE)
				sd.crc32 = match_lenient(dir, sd.fname);
				d_log("copysfv: Got filename (%s) without crc, calculated to %X.\n", sd.fname, sd.crc32);
#else
				d_log("copysfv: Got filename (%s) without crc - ignoring file.\n", sd.fname);
				continue;
#endif
			}


			/* get file extension */
			ptr = find_last_of(fbuf, ".");
			if (*ptr == '.')
				ptr++;

			if (!strcomp(ignored_types, ptr) && !(strcomp(allowed_types, ptr) && !matchpath(allowed_types_exemption_dirs, raceI->misc.current_path)) && !strcomp("sfv", ptr) && !strcomp("nfo", ptr)) {

				skip = 0;
//#if ( sfv_dupecheck == TRUE )
				/* read from sfvdata - no parsing */
				lseek(outfd, 0L, SEEK_SET);
				while (read(outfd, &tempsd, sizeof(SFVDATA)))
//					if (!strcmp(sd.fname, tempsd.fname) || (sd.crc32 == tempsd.crc32 && sd.crc32))
					if (!strcmp(sd.fname, tempsd.fname))
						skip = 1;

				lseek(outfd, 0L, SEEK_END);

#if ( sfv_dupecheck == TRUE )
				if (skip)
					continue;
#endif

				d_log("copysfv:  File in sfv: '%s' (%x)\n", sd.fname, sd.crc32);

#if ( sfv_cleanup == TRUE )
				/* write good stuff to .tmpsfv */
				if (tmpfd != -1) {
					sprintf(crctmp, "%.8x", sd.crc32);
					if (write(tmpfd, sd.fname, strlen(sd.fname)) != (int)strlen(sd.fname))
						d_log("copysfv: write failed: %s\n", strerror(errno));
					if (write(tmpfd, " ", 1) != 1)
						d_log("copysfv: write failed: %s\n", strerror(errno));
					if (write(tmpfd, crctmp, 8) != 8)
						d_log("copysfv: write failed: %s\n", strerror(errno));
#if (sfv_cleanup_crlf == TRUE )
					if (write(tmpfd, "\r", 1) != 1)
						d_log("copysfv: write failed: %s\n", strerror(errno));
#endif
					if (write(tmpfd, "\n", 1) != 1)
						d_log("copysfv: write failed: %s\n", strerror(errno));
				}
#endif

				if (strcomp(audio_types, ptr))
					music++;
				else if (israr(ptr))
					rars++;
				else if (strcomp(video_types, ptr))
					video++;
				else
					others++;

#if ( create_missing_files == TRUE )
				if (!findfile(dir, sd.fname) && !(matchpath(allowed_types_exemption_dirs, raceI->misc.current_path) && strcomp(allowed_types, ptr)))
					create_missing(sd.fname);
#endif

				if (write(outfd, &sd, sizeof(SFVDATA)) != sizeof(SFVDATA))
					d_log("copysfv: write failed: %s\n", strerror(errno));
			}
		}
	}

	if (music > rars) {
		if (video > music)
			type = (video >= others ? RTYPE_VIDEO : RTYPE_OTHER);
		else
			type = (music >= others ? RTYPE_AUDIO : RTYPE_OTHER);
	} else {
		if (video > rars)
			type = (video >= others ? RTYPE_VIDEO : RTYPE_OTHER);
		else
			type = (rars >= others ? RTYPE_RAR : RTYPE_OTHER);
	}

#if ( sfv_cleanup == FALSE )
END:
#endif
#if ( sfv_cleanup == TRUE )
	if (tmpfd != -1) {
		close(tmpfd);
		unlink(source);
		rename(".tmpsfv", source);
	}
#endif

	closedir(dir);
	close(outfd);
	fclose(insfv);
	if (!update_lock(raceI, 1, type)) {
		d_log("copysfv: Lock is suggested removed. Will comply and exit\n");
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}
	raceI->data_type = type;
	return retval;
}

/*
 * Modified	: 01.17.2002 Author	: Dark0n3
 *
 * Description	: Creates a file that contains list of files in release in
 * alphabetical order.
 */
void
create_indexfile(const char *racefile, struct VARS *raceI, char *f)
{
	int		fd;
	FILE		*r;
	int		l, n, m, c;
	int		pos[raceI->total.files],
			t_pos[raceI->total.files];
	char		fname[raceI->total.files][NAME_MAX];

	RACEDATA	rd;

	if ((fd = open(racefile, O_RDONLY)) == -1) {
		d_log("create_indexfile: open(%s): %s\n", racefile, strerror(errno));
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	if (!update_lock(raceI, 1, 0)) {
		d_log("create_indexfile: Lock is suggested removed. Will comply and exit\n");
		close(fd);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	/* Read filenames from race file */
	c = 0;
	while ((read(fd, &rd, sizeof(RACEDATA)))) {
		if (rd.status == F_CHECKED) {
			strlcpy(fname[c], rd.fname, NAME_MAX);
			t_pos[c] = 0;
			c++;
		}
	}
	close(fd);

	/* Sort with cache */
	for (n = 0; n < c; n++) {
		m = t_pos[n];
		for (l = n + 1; l < c; l++) {
			if (strcasecmp(fname[l], fname[n]) < 0)
				m++;
			else
				t_pos[l]++;
		}
		pos[m] = n;
	}

	/* Write to file and free memory */
	if ((r = fopen(f, "w"))) {
		for (n = 0; n < c; n++) {
			m = pos[n];
			fprintf(r, "%s\n", fname[m]);
		}
		fclose(r);
	}
}

/*
 * Modified	: 01.16.2002 Author	: Dark0n3
 *
 * Description	: Marks file as deleted.
 *
 * Obsolete?
 */
short int
clear_file(const char *path, char *f)
{
	int		n = 0, count = 0, retval = -1;
	FILE           *file;

	RACEDATA	rd;

	if ((file = fopen(path, "r+"))) {
		while (fread(&rd, sizeof(RACEDATA), 1, file)) {
#if (sfv_cleanup_lowercase)
			if (sizeof(rd.fname) && strncasecmp(rd.fname, f, NAME_MAX) == 0) {
#else
			if (sizeof(rd.fname) && strncmp(rd.fname, f, NAME_MAX) == 0) {
#endif
				rd.status = F_DELETED;
				fseek(file, sizeof(RACEDATA) * count, SEEK_SET);
				if ((retval = fwrite(&rd, sizeof(RACEDATA), 1, file)) != 1)
					d_log("clear_file: write failed (%d != %d): %s\n", sizeof(RACEDATA), retval, strerror(errno));
				n++;
			}
			count++;
		}
		fclose(file);
	}

	return n;
}

/*
 * Modified	: 02.19.2002 Author	: Dark0n3
 *
 * Description	: Reads current race statistics from fixed format file.
 * 				: "path" is the location of a racedata file.
 */
void
readrace(const char *path, struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI)
{
	int		fd, rlength = 0;

	RACEDATA	rd;

	if ((fd = open(path, O_RDONLY)) != -1) {

		if (!update_lock(raceI, 1, 0)) {
			d_log("readrace: Lock is suggested removed. Will comply and exit\n");
			close(fd);
			remove_lock(raceI);
			exit(EXIT_FAILURE);
		}

		while ((rlength = read(fd, &rd, sizeof(RACEDATA)))) {
			if (rlength != sizeof(RACEDATA)) {
				d_log("readrace: Agh! racedata seems to be broken!\n");
				close(fd);
				remove_lock(raceI);
				exit(EXIT_FAILURE);
			}
			switch (rd.status) {
				case F_NOTCHECKED:
				case F_CHECKED:
					updatestats(raceI, userI, groupI, rd.uname, rd.group,
						    rd.size, (unsigned long)rd.speed, rd.start_time);
					break;
				case F_BAD:
					raceI->total.files_bad++;
					raceI->total.bad_size += rd.size;
					break;
				case F_NFO:
					raceI->total.nfo_present = 1;
					break;
			}
		}
		close(fd);
	}
}

/*
 * Modified	: 01.18.2002 Author	: Dark0n3
 *
 * Description	: Writes stuff into race file.
 */
void
writerace(const char *path, struct VARS *raceI, unsigned int crc, unsigned char status)
{
	int		fd, count, ret;

	RACEDATA	rd;

	d_log("writerace: writing racedata to file %s\n", path);
	/* create file if it doesn't exist */
	if ((fd = open(path, O_CREAT | O_RDWR, 0666)) == -1) {
		if (errno != EEXIST) {
			d_log("writerace: open(%s): %s\n", path, strerror(errno));
			remove_lock(raceI);
			exit(EXIT_FAILURE);
		}
	}

	if (!update_lock(raceI, 1, 0)) {
		d_log("writerace: Lock is suggested removed. Will comply and exit\n");
		close(fd);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}

	/* find an existing entry that we will overwrite */
	count = 0;
	while ((ret = read(fd, &rd, sizeof(RACEDATA)))) {
		if (ret == -1) {
			d_log("writerace: read(%s): %s\n", path, strerror(errno));
			close(fd);
			remove_lock(raceI);
			exit(EXIT_FAILURE);
		}
#if (sfv_cleanup_lowercase)
		if (strncasecmp(rd.fname, raceI->file.name, NAME_MAX) == 0) {
#else
		if (strncmp(rd.fname, raceI->file.name, NAME_MAX) == 0) {
#endif
			lseek(fd, sizeof(RACEDATA) * count, SEEK_SET);
			break;
		}
		count++;
	}

	bzero(&rd, sizeof(RACEDATA));
	rd.status = status;
	rd.crc32 = crc;

	strlcpy(rd.fname, raceI->file.name, NAMEMAX);
	strlcpy(rd.uname, raceI->user.name, 24);
	strlcpy(rd.group, raceI->user.group, 24);
	rd.size = raceI->file.size;
	rd.speed = raceI->file.speed;
	rd.start_time = raceI->total.start_time;

	if (write(fd, &rd, sizeof(RACEDATA)) != sizeof(RACEDATA))
		d_log("writerace: write failed: %s\n", strerror(errno));

	close(fd);
}

/* remove file entry from racedata file */
void
remove_from_race(const char *path, const char *f, struct VARS *raceI)
{
	int		fd, i, max;

	RACEDATA	rd, *tmprd = 0;

	if ((fd = open(path, O_RDONLY)) == -1) {
		d_log("remove_from_race: open(%s): %s\n", path, strerror(errno));
		return;
	}

	for (i = 0; (read(fd, &rd, sizeof(RACEDATA)));) {
#if (sfv_cleanup_lowercase)
		if (strcasecmp(rd.fname, f) != 0) {
#else
		if (strcmp(rd.fname, f) != 0) {
#endif
			tmprd = ng_realloc(tmprd, sizeof(RACEDATA)*(i+1), 0, 1, raceI, 0);
			memcpy(&tmprd[i], &rd, sizeof(RACEDATA));
			i++;
		}
	}

	close(fd);

	if ((fd = open(path, O_TRUNC | O_WRONLY)) == -1) {
		d_log("remove_from_race: open(%s): %s\n", path, strerror(errno));
		ng_free(tmprd);
		return;
	}

	max = i;
	for (i = 0; i < max; i++)
		if (write(fd, &tmprd[i], sizeof(RACEDATA)) != sizeof(RACEDATA))
			d_log("remove_from_race: write failed: %s\n", strerror(errno));

	close(fd);
	ng_free(tmprd);
}

int
verify_racedata(const char *path, struct VARS *raceI)
{
	int		fd, i, ret, max;

	RACEDATA	rd, *tmprd = 0;

	if ((fd = open(path, O_RDWR, 0666)) == -1) {
		d_log("verify_racedata: open(%s): %s\n", path, strerror(errno));
		return 0;
	}

	for (i = 0; (ret = read(fd, &rd, sizeof(RACEDATA)));) {
		d_log("  verify_racedata: Verifying %s..\n", rd.fname);
		if (!strlen(rd.fname)) {
			d_log("  verify_racedata: ERROR! Something is wrong with the racedata!\n");
		} else if (fileexists(rd.fname)) {
			tmprd = ng_realloc(tmprd, sizeof(RACEDATA)*(i+1), 0, 1, raceI, 0);
			memcpy(&tmprd[i], &rd, sizeof(RACEDATA));
			i++;
		} else {
			d_log("verify_racedata: Oops! %s is missing - removing from racedata (ret=%d)\n", rd.fname, ret);
			create_missing(rd.fname);
		}
	}

	close(fd);

	if ((fd = open(path, O_TRUNC | O_WRONLY)) == -1) {
		d_log("verify_racedata: open(%s): %s\n", path, strerror(errno));
		ng_free(tmprd);
		return 0;
	}

	max = i;
	d_log("  verify_racedata: write(%s)\n", path);
	for (i = 0; i < max; i++) {
		d_log("  verify_racedata: write (%i)\n", i);
		if (write(fd, &tmprd[i], sizeof(RACEDATA)) != sizeof(RACEDATA))
			d_log("remove_from_race: write failed: %s\n", strerror(errno));
		d_log("  verify_racedata: write (%i) done.\n", i);
	}
	close(fd);
	d_log("  verify_racedata: write(%s) done.\n", path);
	ng_free(tmprd);

	return 1;
}

/* Locking mechanism and version control.
 * Not yet fully functional, but we're getting there.
 * progtype == a code for what program calls the lock is found in constants.h
 * force_lock == int used to suggest/force a lock on the file.
 *		set to 1 to suggest a lock,2 to force a lock, 3 to put in queue.
 */

int
create_lock(struct VARS *raceI, const char *path, unsigned int progtype, unsigned int force_lock, unsigned int queue)
{
	int		fd, cnt;
	HEADDATA	hd;
	struct stat	sp, sb;
	char		lockfile[PATH_MAX + 1];

	/* this should really be moved out of the proc - we'll worry about it later */
	snprintf(raceI->headpath, PATH_MAX, "%s/%s/headdata", storage, path);

	if ((fd = open(raceI->headpath, O_CREAT | O_RDWR, 0666)) == -1) {
		d_log("create_lock: open(%s): %s\n", raceI->headpath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	snprintf(lockfile, PATH_MAX, "%s.lock", raceI->headpath);
	if (!stat(lockfile, &sp) && (time(NULL) - sp.st_ctime >= max_seconds_wait_for_lock * 5))
		unlink(lockfile);
	cnt = 0;
	while (cnt < 10 && link(raceI->headpath, lockfile)) {
		cnt++;
		d_log("create_lock: link failed (%d/10) - sleeping .1 seconds: %s\n", cnt, strerror(errno));
		usleep(100000);
	}
	if (cnt == 10 ) {
		close(fd);
		d_log("create_lock: link failed: %s\n", strerror(errno));
		return -1;
	} else if (cnt)
		d_log("create_lock: link ok.\n");

	fstat(fd, &sb);
	if (!sb.st_size) {
		/* no lock file exists - let's create one with default values. */
		hd.data_version = sfv_version;
		raceI->data_type = hd.data_type = 0;
		raceI->data_in_use = hd.data_in_use = progtype;
		raceI->data_incrementor = hd.data_incrementor = 1;
		raceI->data_queue = hd.data_queue = 1;
		hd.data_qcurrent = 0;
		raceI->misc.data_completed = hd.data_completed = 0;
		hd.data_pid = (unsigned int)getpid();
		if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
			d_log("create_lock: write failed: %s\n", strerror(errno));
		close(fd);
		d_log("create_lock: lock set. (no previous lockfile found) pid: %d\n", hd.data_pid);
		return 0;
	} else {
		if (read(fd, &hd, sizeof(HEADDATA)) == -1) {
			d_log("create_lock: read() failed: %s\n", strerror(errno));
		}
		if (hd.data_version != sfv_version) {
			d_log("create_lock: version of datafile mismatch. Stopping and suggesting a cleanup.\n");
			close(fd);
			unlink(lockfile);
			return 1;
		}
		if ((time(NULL) - sb.st_ctime >= max_seconds_wait_for_lock * 5)) {
			raceI->misc.release_type = hd.data_type;
			raceI->data_in_use = hd.data_in_use = progtype;
			raceI->data_incrementor = hd.data_incrementor = 1;
			raceI->data_queue = hd.data_queue = 1;
			hd.data_qcurrent = 0;
			raceI->misc.data_completed = hd.data_completed;
			hd.data_pid = (unsigned int)getpid();
			lseek(fd, 0L, SEEK_SET);
			if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
				d_log("create_lock: write failed: %s\n", strerror(errno));
			close(fd);
			d_log("create_lock: lock set. (lockfile exceeded max life time) pid: %d\n", hd.data_pid);
			return 0;
		}
		if (hd.data_in_use) {						/* the lock is active */
			if (force_lock == 2) {
				raceI->data_queue = hd.data_queue = 1;
				hd.data_qcurrent = 0;
				d_log("create_lock: Unlock forced.\n");
			} else {
				if (force_lock == 3) {				/* we got a request to queue a lock if active */
					raceI->data_queue = hd.data_queue;	/* we give the current queue number to the calling process */
					hd.data_queue++;			/* we increment the number in the queue */
					lseek(fd, 0L, SEEK_SET);
					if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
						d_log("create_lock: write failed: %s\n", strerror(errno));
					d_log("create_lock: lock active - putting you in queue. (%d/%d)\n", hd.data_qcurrent, hd.data_queue);
				}
				raceI->misc.release_type = hd.data_type;
				raceI->misc.data_completed = hd.data_completed;
				close(fd);
				return hd.data_in_use;
			}
		}
		if (!hd.data_in_use) {						/* looks like the lock is inactive */
			if (force_lock == 2) {
				raceI->data_queue = hd.data_queue = 1;
				hd.data_qcurrent = 0;
				d_log("create_lock: Unlock forced.\n");
			} else if (force_lock == 3 && hd.data_queue > hd.data_qcurrent) {		/* we got a request to queue a lock if active, */
										/* and there seems to be others in queue. Will not allow the */
										/* process to lock, but wait for the queued process to do so. */
				raceI->data_queue = hd.data_queue;		/* we give the queue number to the calling process */
				hd.data_queue++;				/* we increment the number in the queue */
				raceI->data_incrementor = hd.data_incrementor;
				raceI->misc.release_type = hd.data_type;
				raceI->misc.data_completed = hd.data_completed;
				lseek(fd, 0L, SEEK_SET);
				if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
					d_log("create_lock: write failed: %s\n", strerror(errno));
				close(fd);
				d_log("create_lock: putting you in queue. (%d/%d)\n", hd.data_qcurrent, hd.data_queue);
				unlink(lockfile);
				return -1;
			} else if (hd.data_queue && (queue > hd.data_qcurrent) && !force_lock) {
										/* seems there is a queue, and the calling process' place in */
										/* the queue is still less than current. */
				raceI->data_incrementor = hd.data_incrementor;	/* feed back the current incrementor */
				raceI->misc.release_type = hd.data_type;
				raceI->misc.data_completed = hd.data_completed;
				close(fd);
				unlink(lockfile);
				return -1;
			}
		}
		if (force_lock == 1) {						/* lock suggested - reseting the incrementor to 0 */
			d_log("create_lock: Unlock suggested.\n");
			hd.data_incrementor = 0;
		} else {							/* either no lock and queue, or unlock is forced. */
			hd.data_incrementor = 1;
			hd.data_in_use = progtype;
		}
		raceI->data_incrementor = hd.data_incrementor;
		raceI->misc.data_completed = hd.data_completed;
		raceI->misc.release_type = hd.data_type;
		hd.data_pid = (unsigned int)getpid();
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
			d_log("create_lock: write failed: %s\n", strerror(errno));
		close(fd);
		raceI->data_in_use = progtype;
		d_log("create_lock: lock set. pid: %d\n", hd.data_pid);
		return 0;
	}
}

/* Remove the lock
 */
void
remove_lock(struct VARS *raceI)
{
	if (!raceI->data_in_use)
		d_log("remove_lock: lock not removed - no lock was set\n");
	else {
		int		fd;
		HEADDATA	hd;
		char		lockfile[PATH_MAX + 1];

		if ((fd = open(raceI->headpath, O_RDWR, 0666)) == -1) {
			d_log("remove_lock: open(%s): %s\n", raceI->headpath, strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (read(fd, &hd, sizeof(HEADDATA)) == -1) {
			d_log("remove_lock: read() failed: %s\n", strerror(errno));
			hd.data_queue = 0;
			hd.data_qcurrent = 0;
		}

		hd.data_in_use = 0;
		hd.data_pid = 0;
		hd.data_completed = raceI->misc.data_completed;
		hd.data_incrementor = 0;
		if (hd.data_queue)			/* if queue, increase the number in current so the next */
			++hd.data_qcurrent;		/* process can start. */
		if (hd.data_queue < hd.data_qcurrent) {	/* If the next in line is bigger than the queue itself, */
			hd.data_queue = 0;		/* it should be fair to assume there is noone else in queue */
			hd.data_qcurrent = 0;		/* and reset the queue. Normally, this should not happen. */
		}
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
			d_log("remove_lock: write failed: %s\n", strerror(errno));
		close(fd);
		snprintf(lockfile, sizeof lockfile, "%s.lock", raceI->headpath);
		unlink(lockfile);
		d_log("remove_lock: queue %d/%d\n", hd.data_qcurrent, hd.data_queue);
	}
}

/* update a lock. This should be used after each file checked.
 * This procs task is mainly to 'touch' the lock and to check that nothing else wants the lock.
 * Please note:
 *   if counter == 0 a suggested lock-removal will be written. if >0 it's used as normal.
 *   if datatype != 0, this datatype will be written.
 */

int
update_lock(struct VARS *raceI, unsigned int counter, unsigned int datatype)
{
	int		fd, retval;
	HEADDATA	hd;
	struct stat	sb;

	if (!raceI->headpath[0]) {
		d_log("update_lock: variable 'headpath' empty - assuming no lock is set\n");
		return -1;
	}

	if (!raceI->data_in_use) {
		d_log("update_lock: not updating lock - no lock set\n");
		return 1;
	}

	if ((fd = open(raceI->headpath, O_RDWR, 0666)) == -1) {
		d_log("update_lock: open(%s): %s\n", raceI->headpath, strerror(errno));
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}
	if (read(fd, &hd, sizeof(HEADDATA)) == -1) {
		d_log("update_lock: read() failed: %s\n", strerror(errno));
	}

	if (hd.data_version != sfv_version) {
		d_log("create_lock: version of datafile mismatch. Stopping and suggesting a cleanup.\n");
		close(fd);
		return 1;
	}
	if ((hd.data_in_use != raceI->data_in_use) && counter) {
		d_log("update_lock: Lock not active or progtype mismatch - no choice but to exit.\n");
		close(fd);
		remove_lock(raceI);
		exit(EXIT_FAILURE);
	}
	if (!hd.data_incrementor) {
		d_log("update_lock: Lock suggested removed by a different process (%d/%d).\n", hd.data_incrementor, raceI->data_incrementor);
		retval = 0;
	} else {
		if (counter)
			hd.data_incrementor++;
		else
			hd.data_incrementor = 0;

		retval = hd.data_incrementor;
	}
	raceI->misc.release_type = hd.data_type;
	raceI->misc.data_completed = hd.data_completed;
	if (hd.data_pid != (unsigned int)getpid() && hd.data_incrementor) {
		d_log("update_lock: Oops! Race condition - another process has the lock. pid: %d != %d\n", hd.data_pid, (unsigned int)getpid());
		hd.data_queue = raceI->data_queue - 1;
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
			d_log("create_lock: write failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	if (datatype && counter)
		hd.data_type = datatype;

	fstat(fd, &sb);
	if ((retval && !lock_optimize) || datatype || !retval || !hd.data_incrementor || (time(NULL) - sb.st_ctime >= lock_optimize && hd.data_incrementor > 1)) {
		lseek(fd, 0L, SEEK_SET);
		if (write(fd, &hd, sizeof(HEADDATA)) != sizeof(HEADDATA))
			d_log("create_lock: write failed: %s\n", strerror(errno));
		d_log("update_lock: updating lock (%d)\n", raceI->data_incrementor);
	}
	close(fd);
	if (counter) {
		raceI->data_incrementor = hd.data_incrementor;
		raceI->data_in_use = hd.data_in_use;
	}
	return retval;
}

short int
match_file(char *rname, char *f)
{
	int		n;
	FILE	       *file = NULL;

	RACEDATA	rd;

	n = 0;
	if ((file = fopen(rname, "r+"))) {
		while (fread(&rd, sizeof(RACEDATA), 1, file)) {
			if (strncmp(rd.fname, f, NAME_MAX) == 0	&& rd.status == F_CHECKED) {
				d_log("match_file: '%s' == '%s'\n", rd.fname, f);
				n = 1;
				break;
			}
		}
		fclose(file);
	} else {
		d_log("match_file: Error fopen(%s): %s\n", rname, strerror(errno));
	}
	return n;
}

/*
 * check_rarfile - check for password protected rarfiles
 * psxc r2126 (v1)
 */

int
check_rarfile(const char *filename)
{
	int             fd;
	short           HEAD_CRC;
	char            HEAD_TYPE;
	short           HEAD_FLAGS;
	short           HEAD_SIZE;
	long            ADD_SIZE;
	long            block_size;

	if ((fd = open(filename, O_RDONLY)) == -1) {
		d_log("check_rarfile: Failed to open file (%s): %s\n", filename, strerror(errno));
		return 0;
	}
	if (read(fd, &HEAD_CRC, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_TYPE, 1) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_FLAGS, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_SIZE, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (!(HEAD_CRC == 0x6152 && HEAD_TYPE == 0x72 && HEAD_FLAGS == 0x1a21 && HEAD_SIZE == 0x0007)) {
		close(fd);
		return 0;       /* Not a rar file */
	}
	if (HEAD_FLAGS & 0x8000) {
		if (read(fd, &ADD_SIZE, 4) == -1) {
			d_log("check_rarfile: read() failed: %s\n", strerror(errno));
		}
		block_size = HEAD_SIZE + ADD_SIZE;
		lseek(fd, block_size - 11 + 2, SEEK_CUR);
	} else {
		block_size = HEAD_SIZE;
		lseek(fd, block_size - 7 + 2, SEEK_CUR);
	}
	if (read(fd, &HEAD_TYPE, 1) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_FLAGS, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_SIZE, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (HEAD_TYPE != 0x73) {
		close(fd);
		d_log("check_rarfile: Broken file? File has wrong header\n");
		return 0;       /* wrong header - broken(?) */
	}
	if (HEAD_FLAGS & 0x02) {
		if (read(fd, &ADD_SIZE, 4) == -1) {
			d_log("check_rarfile: read() failed: %s\n", strerror(errno));
		}
		block_size = HEAD_SIZE + ADD_SIZE;
		lseek(fd, block_size - 11 + 2, SEEK_CUR);
	} else {
		block_size = HEAD_SIZE;
		lseek(fd, block_size - 7 + 2, SEEK_CUR);
	}
	if (read(fd, &HEAD_TYPE, 1) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (read(fd, &HEAD_FLAGS, 2) == -1) {
		d_log("check_rarfile: read() failed: %s\n", strerror(errno));
	}
	if (HEAD_TYPE != 0x74) {
		close(fd);
		d_log("check_rarfile: Broken file? File has wrong header\n");
		return 0;       /* wrong header - broken(?) */
	}
	if (HEAD_FLAGS & 0x04) {
		close(fd);
		d_log("check_rarfile: %s have pw protection\n", filename);
		return 1;
	}
	d_log("check_rarfile: %s do not have pw protection\n", filename);
	close(fd);
	return 0;
}


/*
 * check_zipfile - check rarfile for pw protection and strips zip for banned files, and extracts .nfo
 * psxc r2126 (v1)
 */
int
check_zipfile(const char *dirname, const char *zipfile, int do_nfo)
{
	int             ret = 0;
	char            path_buf[PATH_MAX], target[PATH_MAX];
#if (extract_nfo)
	char            nfo_buf[NAME_MAX];
	time_t          t = 0;
	struct stat     filestat;
	char           *ext;
#endif
	DIR            *dir;
	struct dirent  *dp;

	if (!(dir = opendir(dirname)))
		return 0;
	while ((dp = readdir(dir))) {
		sprintf(path_buf, "%s/%s", dirname, dp->d_name);
#if (test_for_password)
		if (!ret)
			ret = check_rarfile(path_buf);
#endif
		if (!strncasecmp("file_id.diz", dp->d_name, 11)) {  	// make lowercase
			sprintf(path_buf, "%s/%s", dirname, dp->d_name);
			rename(path_buf, "file_id.diz");
			if (chmod("file_id.diz", 0644))
				d_log("check_zipfile: Failed to chmod %s: %s\n", "file_id.diz", strerror(errno));
			continue;
		}
#if (zip_clean)
		if (filebanned_match(dp->d_name)) {
			d_log("check_zipfile: banned file detected: %s\n", dp->d_name);
			if (!fileexists(zip_bin))
				d_log("check_zipfile: ERROR! Not able to remove banned file from zip - zip_bin (%s) does not exist!\n", zip_bin);
			else {
				sprintf(target, "%s -qqd \"%s\" \"%s\"", zip_bin, zipfile, dp->d_name);
				if (execute(target))
					d_log("check_zipfile: Failed to remove banned (%s) file from zip.\n", dp->d_name);
			}
			continue;
		}
#endif
#if (extract_nfo)
		ext = find_last_of(dp->d_name, ".");
		if (*ext == '.')
			ext++;
		if (strcomp("nfo", ext)) {
			stat(path_buf, &filestat);
			if ((t && filestat.st_ctime < t) || !t) {
				strlcpy(nfo_buf, dp->d_name, NAME_MAX);
				t = filestat.st_ctime;
				continue;
			}
		}
#endif
	}
#if (extract_nfo)
	if (t) {
		if (!do_nfo) {
			sprintf(path_buf, "%s/%s", dirname, nfo_buf);
			strtolower(nfo_buf);
			rename(path_buf, nfo_buf);
			if (chmod(nfo_buf, 0644))
				d_log("check_zipfile: Failed to chmod %s: %s\n", nfo_buf, strerror(errno));
			d_log("check_zipfile: nfo extracted - %s\n", nfo_buf);
		} else
			d_log("check_zipfile: nfo NOT extracted - a nfo already exist in dir\n");
	}
#endif
	rewinddir(dir);
	while ((dp = readdir(dir))) {
		sprintf(path_buf, "%s/%s", dirname, dp->d_name);
		unlink(path_buf);
	}
	closedir(dir);
	rmdir(dirname);
	return ret;
}

void removedir(const char *dirname)
{
	DIR            *dir;
	struct dirent  *dp;
	char            path_buf[PATH_MAX];

	if (!(dir = opendir(dirname)))
		return;
	rewinddir(dir);
	while ((dp = readdir(dir))) {
		sprintf(path_buf, "%s/%s", dirname, dp->d_name);
		unlink(path_buf);
	}
	closedir(dir);
	rmdir(dirname);
	return;
}

/* create a comma separated list of dirnames - used for affils
 * needs a bit of work yet...
 */
void create_dirlist(const char *dirnames, char *affillist, const int limit)
{
	DIR            *dir;
	struct dirent  *dp;
	char		*p = 0, *s = 0, *t = 0;
	int		n = 0, m = 0, q = strlen(dirnames);
	char		*dlist = NULL;

	dlist = ng_realloc2(dlist, q+4, 1, 1, 1);
//	dlist = malloc(q+4);
//	bzero(dlist, q+4);
	s = dlist;
	t = dlist;

	memcpy(dlist, dirnames, q);
	while (m <= q) {
		if (*s == '\0' || *s == ' ') {
			*s = '\0';
			if (!strlen(t) || !(dir = opendir(t))) {
				ng_free(dlist);
				return;
			}
			rewinddir(dir);
			while ((dp = readdir(dir))) {
				if (!strncmp(dp->d_name, ".", 1))
					continue;
				n = strlen(affillist);
				if ((unsigned)(n + strlen(dp->d_name)) < (unsigned)limit) {
					p = affillist + n;
					if (n) {
						*p = ',';
						p++;
					}
					memcpy(p, dp->d_name, strlen(dp->d_name));
				} else {
					d_log("create_dirlist: Too many dirs - unable to add more.\n");
					d_log("create_dirlist: List so far = '%s'\n", affillist);
					ng_free(dlist);
					return;
				}
			}
			closedir(dir);
			d_log("create_dirlist: List so far = '%s'\n", affillist);
			s++;
			t = s;
		}
		s++;
		m++;
	}
	ng_free(dlist);
	return;
}

/*
 * lenient_compare - compare two filenames - ignore case and mix certain chars depending on config.
 * psxc r2173 (v1)
 */
int
lenient_compare(char *name1, char *name2)
{
	int x = strlen(name1);
	char a[2];
	char b[2];

	a[1] = b[1] = '\0';

	if (strlen(name1) != strlen(name2))
		return 0;

	while (x) {
		a[0] = name1[x - 1];
		b[0] = name2[x - 1];
		if (a[0] != b[0]) {
#if (sfv_cleanup_lowercase)
			strtolower(a);
			strtolower(b);
#endif
#if (sfv_lenient)
			if (a[0] == ' ' || a[0] == ',' || a[0] == '.' || a[0] == '-' || a[0] == '_')
				a[0] = '*';
			if (b[0] == ' ' || b[0] == ',' || b[0] == '.' || b[0] == '-' || a[0] == '_')
				b[0] = '*';
#endif
			if (a[0] != b[0])
				return 0;
		}
		x--;
	}
	return 1;
}

/*
 * Read the data-type from headdata
 * mod by |DureX|, edited by psxc
 */
int
read_headdata(const char *headpath)
{
	int fd = 0;
	HEADDATA hd;

	if ((fd = open(headpath, O_RDONLY)) == -1) {
		d_log("read_headdata: failed to open(%s): %s - returning '0' as data_type\n", headpath, strerror(errno));
		return 0;
	}
	if ((read(fd, &hd, sizeof(HEADDATA))) != sizeof(HEADDATA)) {
		d_log("read_headdata: failed to read %s : %s - returning '0' as data_type\n", headpath, strerror(errno));
		return 0;
	}
	close(fd);

	return hd.data_type;
}

/*
 * First version: 2013.07.21 by Sked
 * Description	: Parses an sfv, testing any present files and
 * removing no-sfv links and -missing files
 * Returns 0 if everything went fine, 2 if there were problems with the SFV
 */
int
parse_sfv(char *sfvfile, GLOBAL *g, DIR *dir) {
	int cnt, cnt2;
	char *ext = 0;
	struct dirent *dp;

	d_log("parse_sfv: Parsing sfv and creating sfv data from %s\n", sfvfile);
	if (copysfv(sfvfile, g->l.sfv, &g->v)) {
		d_log("parse_sfv: Found invalid entries in SFV.\n");
		mark_as_bad(sfvfile);
		unlink(g->l.race);
		unlink(g->l.sfv);

		rewinddir(dir);
		while ((dp = readdir(dir))) {
			cnt = cnt2 = (int)strlen(dp->d_name);
			ext = dp->d_name;
			while (ext[cnt] != '-' && cnt > 0)
				cnt--;
			if (ext[cnt] != '-')
				cnt = cnt2;
			else
				cnt++;
			ext += cnt;
			if (!strncmp(ext, "missing", 7))
				unlink(dp->d_name);
		}
		return 2;
	}

#if ( create_missing_sfv_link == TRUE )
	if (g->l.sfv_incomplete) {
		d_log("parse_sfv: Removing missing-sfv indicator (if any)\n");
		unlink(g->l.sfv_incomplete);
	}
#endif

#if (use_partial_on_noforce == TRUE)
	if ( (force_sfv_first == FALSE) || matchpartialpath(noforce_sfv_first_dirs, g->l.path))
#else
	if ( (force_sfv_first == FALSE) || matchpath(noforce_sfv_first_dirs, g->l.path))
#endif
	{
		if (fileexists(g->l.race) && fileexists(g->l.sfv)) {
			d_log("parse_sfv: Testing files marked as untested\n");
			testfiles(&g->l, &g->v, 0);
		}
	}
	d_log("parse_sfv: Reading file count from SFV\n");
	readsfv(g->l.sfv, &g->v, 0);

	if (g->v.total.files == 0) {
		d_log("parse_sfv: SFV seems to have no files of accepted types, or has errors.\n");
		unlink(g->l.sfv);
		mark_as_bad(sfvfile);
		return 2;
	}

	if (fileexists(g->l.race)) {
		d_log("parse_sfv: Reading race data from file to memory\n");
		readrace(g->l.race, &g->v, g->ui, g->gi);
	}
	d_log("parse_sfv: Making sure that release is not marked as complete\n");
	removecomplete(g->v.misc.release_type);

	if (deny_resume_sfv == TRUE) {
		if (copyfile(sfvfile, g->l.sfvbackup))
			d_log("parse_sfv: failed to make backup of sfv (%s)\n", sfvfile);
		else
			d_log("parse_sfv: created backup of sfv (%s)\n", sfvfile);
	}

	return 0;
}
