// word_filter.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdlib.h>
#include <malloc.h>
#include <cctype>
#include <string.h>

static int rejectShort = 0;
static int rejectLong = 0;
static int rejectHex = 0;
static int rejectBad = 0;

static const char * badWords[] = {
	"adult",
	"Affair",
	"aids",
	"American",
	"Angel",
	"Bastard",
	"Beer",
	"Bible",
	"Black",
	"Blue",
	"Body",
	"Boy",
	"Capitalism",
	"Champagne",
	"Cheque",
	"Christian",
	"Church",
	"Conception",
	"Crack",
	"Creation",
	"Disability",
	"Easter",
	"English",
	"Fat",
	"Female",
	"Favour",
	"Fibre",
	"Freedom",
	"French",
	"Gender",
	"Girl",
	"God",
	"Guy",
	"Harbour",
	"Honour",
	"Husband",
	"Infant",
	"Jew",
	"Labour",
	"Lover",
	"Male",
	"Man",
	"Mate",
	"Metre",
	"Miss",
	"Mother",
	"Mrs",
	"Police",
	"Policeman",
	"Pope",
	"Prayer",
	"Pregnancy",
	"President",
	"Programme",
	"Rape",
	"Religion",
	"Sex",
	"Shit",
	"Sin",
	"Sir",
	"Sister",
	"Son",
	"Strip",
	"Stroke",
	"Suicide",
	"Tonne",
	"Violence",
	"War",
	"Weapon",
	"Welfare",
	"Widow",
	"Wife",
	"Woman",
	"Blind",
	"Bust",
	"Crap",
	"EST",
	"Fleshy",
	"Frank",
	"Gay",
	"Ghostly",
	"Grave",
	"Holy",
	"INC",
	"Nude",
	"Queer",
	"Sexy",
	"Sleazy",
	"Spanish",
	"WAN",
	NULL
};

static bool considerWord(const char * wordStart, const char * wordEnd)
{
	int length = wordEnd - wordStart;

	if (length < 3)
	{
		++rejectShort;
		return false;
	}
	if (length > 8)
	{
		++rejectLong;
		return false;
	}

	bool hasNonHex = false;

	const char * pWord = wordStart;

	while (pWord < wordEnd && !hasNonHex)
	{
		char letter = tolower(*pWord);
		hasNonHex = (letter > 'f') || (letter < 'a');
		++pWord;
	}

	if (!hasNonHex)
	{
		++rejectHex;
		return false;
	}

	int badWordIndex = 0;
	while (badWords[badWordIndex])
	{
		if (!_strnicmp(badWords[badWordIndex], wordStart, length))
		{
			++rejectBad;
			return false;
		}
		++badWordIndex;
	}

	return true;
}

static int __cdecl compareString(const void * w1, const void * w2)
{
	return strcmp((const char *)*(const void **)w1, (const char *)*(const void **)w2);
}

static int filter_words()
{
	int wordsFound = 0;
	int wordsToFind = 1024 * 4;
	int wordsToIgnore = 0; // 20;
	int wordPosition = 1;

	FILE * fptr = NULL;
	//errno_t err = fopen_s(&fptr, "20k.txt", "rb");
	errno_t err = fopen_s(&fptr, "2knouns.txt", "rb");
	//errno_t err = fopen_s(&fptr, "500adj.txt", "rb");

	if (err != 0)
	{
		printf("Could not open 20k.txt\n");
		return -1;
	}

	int seekResult = fseek(fptr, 0, SEEK_END);
	if (seekResult != 0)
	{
		printf("Could not determine file size\n");
		fclose(fptr);
		return -2;
	}

	int fsize = ftell(fptr);
	seekResult = fseek(fptr, 0, SEEK_SET);

	if (seekResult != 0)
	{
		printf("Could not reset file pointer\n");
		fclose(fptr);
		return -3;
	}

	char * fbuffer = (char *)malloc(fsize);
	if (NULL == fbuffer)
	{
		printf("Could allocate buffer\n");
		fclose(fptr);
		return -4;
	}

	int numRead = fread(fbuffer, 1, fsize, fptr);

	if (numRead != fsize)
	{
		printf("error reading file\n");
		fclose(fptr);
		return -5;
	}

	char * wordStart = fbuffer;
	char * listEnd = &(fbuffer[fsize]);

	char ** wordList = (char **)malloc(sizeof(char *) * wordsToFind);
	if (wordList == NULL)
	{
		printf("could not allocate word index\n");
		fclose(fptr);
		return -6;
	}

	while ((wordStart < listEnd) && (wordsFound < wordsToFind))
	{
		//skip the leading words on the line as needed
		int wordCount = 0;
		while (wordCount < wordPosition)
		{
			while ((wordStart < listEnd) && (*wordStart != ' ') && (*wordStart != 0x0D) && (*wordStart != 0x0A))
			{
				++wordStart;
			}

			while ((wordStart < listEnd) && (*wordStart == ' '))
			{
				++wordStart;
			}
			++wordCount;
		}

		char * wordEnd = wordStart;
		while ((wordEnd < listEnd) && (*wordEnd != 0x0A) && (*wordEnd != 0x0D) && (*wordEnd != ' '))
		{
			++wordEnd;
		}

		if (wordsToIgnore > 0)
		{
			--wordsToIgnore;
		}
		else if (considerWord(wordStart, wordEnd))
		{
			//printf("%.*s\n", wordEnd - wordStart, wordStart);
			*wordEnd = 0;
			wordList[wordsFound] = wordStart;
			while (wordStart < wordEnd)
			{
				*wordStart = tolower(*wordStart);
				++wordStart;
			}
			++wordsFound;
		}

		//advance to the end of the line
		while ((wordEnd < listEnd) && (*wordEnd != 0x0A) && (*wordEnd != 0x0D))
		{
			++wordEnd;
		}

		//find the next line
		while ((wordEnd < listEnd) && ((*wordEnd == 0x0A) || (*wordEnd == 0x0D)))
		{
			++wordEnd;
		}

		wordStart = wordEnd;
	}

	qsort(wordList, wordsFound, sizeof(char *), compareString);

	printf("const char * wordList[%d] = {\n", wordsFound);

	int lastWordIndex = (wordsFound - 1);
	int wordIndex;
	for (wordIndex = 0; wordIndex < wordsFound; ++wordIndex)
	{
		printf("    \"%s\"%s\n", wordList[wordIndex], (wordIndex == lastWordIndex) ? "" : ",");
	}
	printf("};\n");

	fprintf(stderr, "%d words (%d short words rejected, %d long words rejected, %d hex words rejected, %d bad words rejected)\n", wordsFound, rejectShort, rejectLong, rejectHex, rejectBad);

	fclose(fptr);
	free(fbuffer);
    return 0;
}


int main()
{
	filter_words();
}
