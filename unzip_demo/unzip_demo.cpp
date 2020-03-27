/* miniunz.c
   Version 1.2.0, September 16th, 2017
   sample part of the MiniZip project

   Copyright (C) 2012-2017 Nathan Moinvaziri
	 https://github.com/nmoinvaz/minizip
   Copyright (C) 2009-2010 Mathias Svensson
	 Modifications for Zip64 support
	 http://result42.com
   Copyright (C) 2007-2008 Even Rouault
	 Modifications of Unzip for Zip64
   Copyright (C) 1998-2010 Gilles Vollant
	 http://www.winimage.com/zLibDll/minizip.html

   This program is distributed under the terms of the same license as zlib.
   See the accompanying LICENSE file for the full text of the license.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

#ifdef _WIN32
#  include <direct.h>
#  include <io.h>
#include <stdint.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  include <utime.h>
#endif

#include "unzip.h"

#ifdef _WIN32
#  define USEWIN32IOAPI
#  include "iowin32.h"
#endif
#include "minishared.h"

//int miniunz_list(unzFile uf)
//{
//	int err = unzGoToFirstFile(uf);
//	if (err != UNZ_OK)
//	{
//		printf("error %d with zipfile in unzGoToFirstFile\n", err);
//		return 1;
//	}
//
//	printf("  Length  Method     Size Ratio   Date    Time   CRC-32     Name\n");
//	printf("  ------  ------     ---- -----   ----    ----   ------     ----\n");
//
//	do
//	{
//		char filename_inzip[256] = { 0 };
//		unz_file_info64 file_info = { 0 };
//		uint32_t ratio = 0;
//		struct tm tmu_date = { 0 };
//		const char *string_method = NULL;
//		char char_crypt = ' ';
//
//		err = unzGetCurrentFileInfo64(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
//		if (err != UNZ_OK)
//		{
//			printf("error %d with zipfile in unzGetCurrentFileInfo\n", err);
//			break;
//		}
//
//		if (file_info.uncompressed_size > 0)
//			ratio = (uint32_t)((file_info.compressed_size * 100) / file_info.uncompressed_size);
//
//		/* Display a '*' if the file is encrypted */
//		if ((file_info.flag & 1) != 0)
//			char_crypt = '*';
//
//		if (file_info.compression_method == 0)
//			string_method = "Stored";
//		else if (file_info.compression_method == Z_DEFLATED)
//		{
//			uint16_t level = (uint16_t)((file_info.flag & 0x6) / 2);
//			if (level == 0)
//				string_method = "Defl:N";
//			else if (level == 1)
//				string_method = "Defl:X";
//			else if ((level == 2) || (level == 3))
//				string_method = "Defl:F"; /* 2:fast , 3 : extra fast*/
//			else
//				string_method = "Unkn. ";
//		}
//		else if (file_info.compression_method == Z_BZIP2ED)
//		{
//			string_method = "BZip2 ";
//		}
//		else
//			string_method = "Unkn. ";
//
//		display_zpos64(file_info.uncompressed_size, 7);
//		printf("  %6s%c", string_method, char_crypt);
//		display_zpos64(file_info.compressed_size, 7);
//
//		dosdate_to_tm(file_info.dos_date, &tmu_date);
//		printf(" %3u%%  %2.2u-%2.2u-%2.2u  %2.2u:%2.2u  %8.8x   %s\n", ratio,
//			(uint32_t)tmu_date.tm_mon + 1, (uint32_t)tmu_date.tm_mday,
//			(uint32_t)tmu_date.tm_year % 100,
//			(uint32_t)tmu_date.tm_hour, (uint32_t)tmu_date.tm_min,
//			file_info.crc, filename_inzip);
//
//		err = unzGoToNextFile(uf);
//	} while (err == UNZ_OK);
//
//	if (err != UNZ_END_OF_LIST_OF_FILE && err != UNZ_OK)
//	{
//		printf("error %d with zipfile in unzGoToNextFile\n", err);
//		return err;
//	}
//
//	return 0;
//}


int miniunz_extract_currentfile(unzFile uf, int opt_extract_without_path, int *popt_overwrite, const char *password)
{
	unz_file_info64 file_info = { 0 };
	FILE* fout = NULL;
	void* buf = NULL;
	uint16_t size_buf = 8192;
	int err = UNZ_OK;
	int errclose = UNZ_OK;
	int skip = 0;
	char filename_inzip[256] = { 0 };
	char *filename_withoutpath = NULL;
	const char *write_filename = NULL;
	char *p = NULL;

	err = unzGetCurrentFileInfo64(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
	if (err != UNZ_OK)
	{
		printf("error %d with zipfile in unzGetCurrentFileInfo\n", err);
		return err;
	}

	p = filename_withoutpath = filename_inzip;
	while (*p != 0)
	{
		if ((*p == '/') || (*p == '\\'))
			filename_withoutpath = p + 1;
		p++;
	}

	/* If zip entry is a directory then create it on disk */
	if (*filename_withoutpath == 0)
	{
		if (opt_extract_without_path == 0)
		{
			printf("creating directory: %s\n", filename_inzip);
			MKDIR(filename_inzip);
		}
		return err;
	}

	buf = (void*)malloc(size_buf);
	if (buf == NULL)
	{
		printf("Error allocating memory\n");
		return UNZ_INTERNALERROR;
	}

	err = unzOpenCurrentFilePassword(uf, password);
	if (err != UNZ_OK)
		printf("error %d with zipfile in unzOpenCurrentFilePassword\n", err);

	if (opt_extract_without_path)
		write_filename = filename_withoutpath;
	else
		write_filename = filename_inzip;

	/* Determine if the file should be overwritten or not and ask the user if needed */
	if ((err == UNZ_OK) && (*popt_overwrite == 0) && (check_file_exists(write_filename)))
	{
		char rep = 0;
		do
		{
			char answer[128];
			printf("The file %s exists. Overwrite ? [y]es, [n]o, [A]ll: ", write_filename);
			if (scanf("%1s", answer) != 1)
				exit(EXIT_FAILURE);
			rep = answer[0];
			if ((rep >= 'a') && (rep <= 'z'))
				rep -= 0x20;
		} while ((rep != 'Y') && (rep != 'N') && (rep != 'A'));

		if (rep == 'N')
			skip = 1;
		if (rep == 'A')
			*popt_overwrite = 1;
	}

	/* Create the file on disk so we can unzip to it */
	if ((skip == 0) && (err == UNZ_OK))
	{
		fout = fopen64(write_filename, "wb");
		/* Some zips don't contain directory alone before file */
		if ((fout == NULL) && (opt_extract_without_path == 0) &&
			(filename_withoutpath != (char*)filename_inzip))
		{
			char c = *(filename_withoutpath - 1);
			*(filename_withoutpath - 1) = 0;
			_mkdir(write_filename);
			*(filename_withoutpath - 1) = c;
			fout = fopen64(write_filename, "wb");
		}
		if (fout == NULL)
			printf("error opening %s\n", write_filename);
	}

	/* Read from the zip, unzip to buffer, and write to disk */
	if (fout != NULL)
	{
		printf(" extracting: %s\n", write_filename);

		do
		{
			err = unzReadCurrentFile(uf, buf, size_buf);
			if (err < 0)
			{
				printf("error %d with zipfile in unzReadCurrentFile\n", err);
				break;
			}
			if (err == 0)
				break;
			if (fwrite(buf, err, 1, fout) != 1)
			{
				printf("error %d in writing extracted file\n", errno);
				err = UNZ_ERRNO;
				break;
			}
		} while (err > 0);

		if (fout)
			fclose(fout);

		/* Set the time of the file that has been unzipped */
		//if (err == 0)
		//	change_file_date(write_filename, file_info.dos_date);
	}

	errclose = unzCloseCurrentFile(uf);
	if (errclose != UNZ_OK)
		printf("error %d with zipfile in unzCloseCurrentFile\n", errclose);

	free(buf);
	return err;
}

int miniunz_extract_all(unzFile uf, int opt_extract_without_path, int opt_overwrite, const char *password)
{
	int err = unzGoToFirstFile(uf);
	if (err != UNZ_OK)
	{
		printf("error %d with zipfile in unzGoToFirstFile\n", err);
		return 1;
	}

	do
	{
		err = miniunz_extract_currentfile(uf, opt_extract_without_path, &opt_overwrite, password);
		if (err != UNZ_OK)
			break;
		err = unzGoToNextFile(uf);
	} while (err == UNZ_OK);

	if (err != UNZ_END_OF_LIST_OF_FILE)
	{
		printf("error %d with zipfile in unzGoToNextFile\n", err);
		return 1;
	}
	return 0;
}

int miniunz_extract_onefile(unzFile uf, const char *filename, int opt_extract_without_path, int opt_overwrite,
	const char *password)
{
	if (unzLocateFile(uf, filename, NULL) != UNZ_OK)
	{
		printf("file %s not found in the zipfile\n", filename);
		return 2;
	}
	if (miniunz_extract_currentfile(uf, opt_extract_without_path, &opt_overwrite, password) == UNZ_OK)
		return 0;
	return 1;
}

#ifndef NOMAIN
#endif

int unzip_file(const char *src_zip_path, const char *filename_to_extract, const char *save_path) {
	int opt_do_extract_withoutpath = 0;
	int opt_overwrite = 0;
	const char *password = NULL;

	/* Open zip file */
	if (src_zip_path != NULL)
	{
		unzFile uf = NULL;
#ifdef USEWIN32IOAPI
		zlib_filefunc64_def ffunc;
		fill_win32_filefunc64A(&ffunc);
		uf = unzOpen2_64(src_zip_path, &ffunc);
#else
		uf = unzOpen64(src_zip_path);
#endif
		if (!uf)
		{
			printf("Cannot open %s\n", src_zip_path);
			return -1;
		}

		printf("%s opened\n", src_zip_path);

		if (_chdir(save_path))
		{
			printf("Error changing into %s, aborting\n", save_path);
			exit(-1);
		}

		int ret = -1;
		if (filename_to_extract == NULL) {
			ret = miniunz_extract_all(uf, opt_do_extract_withoutpath, opt_overwrite, password);
		}
		else {
			ret = miniunz_extract_onefile(uf, filename_to_extract, opt_do_extract_withoutpath, opt_overwrite, password);
		}

		unzClose(uf);
		return ret;
	}

	return 1;
}

int main(int argc, char* argv[]) {
	printf("zlib version=%s\n", zlibVersion());

	const char *files[] = {"file1.txt", "file2.txt" };
	for (int i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		int ret = unzip_file("./test.zip", files[i], "./");
		printf("unzip file %s return %d\n", files[i], ret);
	}
	
	return 0;
}