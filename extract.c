#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <limits.h>
#include <plist/plist.h>

#include <zip.h>
#include "/usr/local/lib/libzip/include/zipconf.h"

int test()
{
	return 0;
}
//#define DBG
#ifdef DBG
#define DEBUG(format...) printf(format)
#else
#define DEBUG(format...) test(format)
#endif
#ifndef ZIP_CODEC_ENCODE
// this is required for old libzip...
#define zip_get_num_entries(x, y) zip_get_num_files(x)
#define zip_int64_t ssize_t
#define zip_uint64_t off_t
#endif

#define ITUNES_METADATA_PLIST_FILENAME "iTunesMetadata.plist"

char *bundleicons = NULL;

static const char base64_str[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char base64_pad = '=';

char iconName[128];
char absPath[256];
static int write_png_data(const char *filename, const char *pngdata, uint32_t size)
{
	size_t amount = 0, written = 0;
	FILE *fp = fopen(filename, "wb+");

	while(written < size)
	{
		amount = fwrite((pngdata+written), 1, size-written, fp);
		written += amount;
		if (written == size)
		{
			DEBUG("written = size\n");
			break;
		}
		//printf("amount=%ld\n", amount);
	}

	fflush(fp);
	fclose(fp);

	return written;
}

/*static int write_png_data(const char *filename, const char **pngdata, uint32_t size)
{
	size_t amount = 0, written = 0;
	FILE *fp = fopen(filename, "wb+");

	while(written < size)
	{
		amount = fwrite(*(pngdata+written), 1, size-written, fp);
		written += amount;
		if (written == size)
		{
			printf("written = size\n");
			break;
		}
		//printf("amount=%ld\n", amount);
	}

	fflush(fp);
	fclose(fp);

	return written;
}*/
//png normalize function
//char pendingIDATChunk[200*1024];
char *pendingIDATChunk = NULL;
int pendingLength = 0;
char *newpng = NULL;
//char newpng[200*1024];
//char *oldpng = NULL;
#include <zlib.h>
#include <arpa/inet.h>
unsigned short pngheader[8]={0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
//unsigned short pngheader[5]={0x5089,0x474E,0x0A0D,0x0A1A};


/*PyDoc_STRVAR(compress__doc__,
"compress(string[, level]) -- Returned compressed string.\n"
"\n"
"Optional arg level is the compression level, in 0-9.");
*/

//static PyObject *
//PyZlib_compress(PyObject *self, PyObject *args)
char *compress_own(char *data, int *output_len, int input_len)
{
    //PyObject *ReturnVal = NULL;
    char *input, *output;
    int length, level=Z_DEFAULT_COMPRESSION, err;
    z_stream zst;

    /* require Python string object, optional 'level' arg */
    /*if (!PyArg_ParseTuple(args, "s#|i:compress", &input, &length, &level))
        return NULL;*/
	//level = 6;
	length = input_len;
	input = data;
    zst.avail_out = length + length/1000 + 12 + 1;

	DEBUG("zst.avail_out=%d\n", zst.avail_out);
    output = (char*)malloc(zst.avail_out);
    if (output == NULL) {
        //PyErr_SetString(PyExc_MemoryError,
        DEBUG("Can't allocate memory to compress data\n");
        return NULL;
    }

    /* Past the point of no return.  From here on out, we need to make sure
       we clean up mallocs & INCREFs. */

    //zst.zalloc = (alloc_func)NULL;
    //zst.zfree = (free_func)Z_NULL;
    zst.zalloc = NULL;
    zst.zfree = Z_NULL;
    zst.next_out = (Byte *)output;
    zst.next_in = (Byte *)input;
    zst.avail_in = length;
    err = deflateInit(&zst, level);

    switch(err) {
    case(Z_OK):
        break;
    case(Z_MEM_ERROR):
        //PyErr_SetString(PyExc_MemoryError,
        DEBUG("Out of memory while compressing data\n");
        goto error;
    case(Z_STREAM_ERROR):
        //PyErr_SetString(ZlibError,
        DEBUG("Bad compression level\n");
        goto error;
    default:
        deflateEnd(&zst);
        //zlib_error(zst, err, "while compressing data");
        DEBUG("while compressing data\n");
        goto error;
    }

    //Py_BEGIN_ALLOW_THREADS;
    err = deflate(&zst, Z_FINISH);
    //Py_END_ALLOW_THREADS;

    if (err != Z_STREAM_END) {
        //zlib_error(zst, err, "while compressing data");
        DEBUG("while compressing data\n");
        deflateEnd(&zst);
        goto error;
    }

    err=deflateEnd(&zst);
    if (err == Z_OK)
    {    //ReturnVal = PyString_FromStringAndSize((char *)output,
        //                                       zst.total_out);
        *output_len = zst.total_out;
        //return zst.next_out;
	return output;
    }
    else
		DEBUG("while finishing compression\n");
        //zlib_error(zst, err, "while finishing compression");

 error:
    free(output);

    return NULL;
}

/*
PyDoc_STRVAR(decompress__doc__,
"decompress(string[, wbits[, bufsize]]) -- Return decompressed string.\n"
"\n"
"Optional arg wbits is the window buffer size.  Optional arg bufsize is\n"
"the initial output buffer size.");
*/

//static PyObject *
//PyZlib_decompress(PyObject *self, PyObject *args)
int global_decompress_err = 0;
char *decompress(char *compressed, int wsize, int input_len, int bufsize)
{
	DEBUG("wsize=%d, input_len=%d, bufsize=%d\n", wsize, input_len, bufsize);
    Byte *result_str;
    Byte *input = compressed;
    int length, err;
    //int wsize=DEF_WBITS;
    //Py_ssize_t r_strlen=DEFAULTALLOC;
    int r_strlen = bufsize;
    length = input_len;
    z_stream zst;

    //if (!PyArg_ParseTuple(args, "s#|in:decompress",
    //                      &input, &length, &wsize, &r_strlen))
    //    return NULL;

    if (r_strlen <= 0)
        r_strlen = 1;

    zst.avail_in = length;
    zst.avail_out = r_strlen;

    //if (!(result_str = PyString_FromStringAndSize(NULL, r_strlen)))
    //    return NULL;
    result_str = (char *)malloc(r_strlen);
	memset(result_str, 0, r_strlen)
	;
    zst.zalloc = (alloc_func)NULL;
    zst.zfree = (free_func)Z_NULL;
    zst.next_out = (Byte *)(result_str);
    zst.next_in = (Byte *)input;
    err = inflateInit2(&zst, wsize);

    switch(err) {
    case(Z_OK):
        break;
    case(Z_MEM_ERROR):
        //PyErr_SetString(PyExc_MemoryError,
        DEBUG("Out of memory while decompressing data\n");
        goto error;
    default:
        inflateEnd(&zst);
        //zlib_error(zst, err, "while preparing to decompress data");
        DEBUG("while preparing to decompress data\n");
        goto error;
    }

    do {
        //Py_BEGIN_ALLOW_THREADS
        err=inflate(&zst, Z_FINISH);
        //Py_END_ALLOW_THREADS
		DEBUG(" inflate: err=%d\n", err);
        switch(err) {
        case(Z_STREAM_END):
            break;
        case(Z_BUF_ERROR):
            /*
             * If there is at least 1 byte of room according to zst.avail_out
             * and we get this error, assume that it means zlib cannot
             * process the inflate call() due to an error in the data.
             */
            if (zst.avail_out > 0) {
                //zlib_error(zst, err, "while decompressing data");
                DEBUG("while decompressing data\n");
                inflateEnd(&zst);
                goto error;
            }
            /* fall through */
        case(Z_OK):
            /* need more memory */
            /*if (_PyString_Resize(&result_str, r_strlen << 1) < 0) {
                inflateEnd(&zst);
                goto error;
            }*/
            //zst.next_out = (unsigned char *)PyString_AS_STRING(result_str)
            zst.next_out = (unsigned char *)(result_str) \
                + r_strlen;
            zst.avail_out = r_strlen;
            r_strlen = r_strlen << 1;
            break;
        default:
            inflateEnd(&zst);
            //zlib_error(zst, err, "while decompressing data");
            DEBUG("%d: while decompressing data, %d\n", __LINE__, err);
            goto error;
        }
    } while (err != Z_STREAM_END);

    err = inflateEnd(&zst);
    if (err != Z_OK) {
        //zlib_error(zst, err, "while finishing data decompression");
        DEBUG("while finishing data decompression\n");
        goto error;
    }

    //_PyString_Resize(&result_str, zst.total_out);
    global_decompress_err = err;
    return result_str;

 error:
    //Py_XDECREF(result_str);
    if (result_str)
    {
		free(result_str);
	}
	global_decompress_err = err;
    return NULL;
}


int pngnormal(char *filename, char *oldpng, long int size)
{
	DEBUG("OldPNG size: %ldKB\n", size/1024);

	int i = 0;
	char *poldpng = oldpng;
	//char *pnewpng = newpng;
	//char *poldpng = malloc(size+1);
	newpng = malloc(size*2+1);
	char *pnewpng = newpng;
	int total_len = 0;
	int old_len = size;
	int new_len = 0;

	int chunkLength = 0;
	char chunkType[4] = {0};
	char *chunkData = NULL;
	char *newdata = NULL;
	char * compressed_data = NULL;
	int chunkCRC = 0;
	int width = 0;
	int height = 0;
	int bufsize = 0;
	
	//memset(poldpng , 0, size+1);
	pendingIDATChunk = malloc(size*2 + 1);
	memset(pendingIDATChunk, 0, size*2+1);
	memset(pnewpng, 0, size*2+1);

	/*DEBUG("PNG[0]=%02x, PNG[1]=%02x, PNG[2]=%02x, PNG[3]=%02x\n"
			"PNG[4]=%02x, PNG[5]=%02x, PNG[6]=%02x, PNG[7]=%02x\n",
			poldpng[0], poldpng[1], poldpng[2], poldpng[3],
			poldpng[4], poldpng[5], poldpng[6], poldpng[7]);*/
	for (i = 1; i< 8; i++)
	{
		if (poldpng[i] != pngheader[i])
		{
			DEBUG("i=%d failed, poldpng=%02x\n", i, poldpng[i]);
			return -1;
		}
	}
	DEBUG("Valid PNG File\n");

	//Copy first 8 bytes to Newpng;
	memcpy(pnewpng, poldpng, 8);

	DEBUG("PNG[0]=%02x, PNG[1]=%02x, PNG[2]=%02x, PNG[3]=%02x\n"
			"PNG[4]=%02x, PNG[5]=%02x, PNG[6]=%02x, PNG[7]=%02x\n",
			pnewpng[0], pnewpng[1], pnewpng[2], pnewpng[3],
			pnewpng[4], pnewpng[5], pnewpng[6], pnewpng[7]);
	new_len = 8;
	poldpng += new_len;
	pnewpng += new_len;
	total_len = new_len;
	while(new_len < old_len)
	{


		//Get chunkLength
		memcpy(&chunkLength, poldpng, 4);
		chunkLength = ntohl(chunkLength);
		DEBUG("ChunkLength=%d\n", chunkLength);

		//Get chunkType
		memcpy(chunkType, poldpng+4, 4);

		DEBUG("chunkType: %02x, %02x, %02x, %02x,\n "
				"%c%c%c%c\n",
			chunkType[0], chunkType[1], chunkType[2], chunkType[3],
			chunkType[0], chunkType[1], chunkType[2], chunkType[3]);

		//Get chunkData
		if (chunkLength > 0)
		{
			chunkData = (char *)malloc(chunkLength+1);
			memcpy(chunkData, poldpng+8, chunkLength);
		}

		//Get chunkCRC
		memcpy(&chunkCRC, poldpng+chunkLength+8, 4);
		chunkCRC = ntohl(chunkCRC);

		//Copy all the old data from oldpng to newpng
		//memcpy(pnewpng, poldpng, chunkLength+12);
		new_len += chunkLength + 12;
		poldpng += chunkLength + 12;

		DEBUG("new_len=%d\n", new_len);
		//pnewpng += new_len;


		//Parsing the header chunk [IHDR]
		if (chunkType[0] == 'I' && chunkType[1] == 'H' &&
			chunkType[2] == 'D' && chunkType[3] == 'R')
		{
			DEBUG("This is IHDR chunk\n");
			memcpy(&width, chunkData, 4);
			width = ntohl(width);
			memcpy(&height, chunkData+4, 4);
			height = ntohl(height);

			DEBUG("Height x Width: %dx%d\n", height, width);
		}

		//Parsing the image chunk [IDAT]
		if (chunkType[0] == 'I' && chunkType[1] == 'D' &&
			chunkType[2] == 'A' && chunkType[3] == 'T')
		{
			DEBUG("This is IDAT chunk\n");
			bufsize = width * height * 4 + height;
			DEBUG("bufsize=%d\n", bufsize);
			char *decompressed_data = (char *)malloc(bufsize+1),
				 *dtmp = NULL;
			memset(decompressed_data, 0, bufsize);
			
			DEBUG("global_decompress_er=%d\n", global_decompress_err);

			if (-5 == global_decompress_err)
			{
				//one png file may have multiple IDAT chunks.
				DEBUG("Got here 1\n");
				memcpy(pendingIDATChunk+pendingLength, chunkData, chunkLength);
				DEBUG("Got here 2\n");
				pendingLength += chunkLength;
				//write_png_data("chunkData2", chunkData, chunkLength);
				//write_png_data("pendingIDATChunk", pendingIDATChunk, pendingLength);
				dtmp = decompress(pendingIDATChunk, -8, pendingLength, bufsize);
			}
			else
			{
				//write_png_data("chunkData", chunkData, chunkLength);
				dtmp = decompress(chunkData, -8, chunkLength, bufsize);
			}
			if (dtmp == NULL)
			{
				DEBUG("Cannot decompress data, error=%d\n", global_decompress_err);
				if (-3 == global_decompress_err)
				{//image data not compressed.
					goto NOT_COMPRESSED;
				}
				else if (-5 == global_decompress_err)
				{
					memcpy(pendingIDATChunk+pendingLength, chunkData, chunkLength);
					pendingLength += chunkLength;
					DEBUG("pendingLength=%d\n", pendingLength);
					if(chunkData)
					{
						free(chunkData);
					}
					continue;
				}
			}
			else
			{
				memset(pendingIDATChunk, 0, sizeof(pendingIDATChunk));
				pendingLength = 0;
			}
			memcpy(decompressed_data, dtmp, bufsize);


			//Swapping red & blue bytes for each pixel
			newdata = (char *)malloc(bufsize+1);
			int x, y, i, len_newdata = 0;

			memset(newdata, 0, bufsize);

			for (y=0; y< height; y++)
			{
				i = len_newdata;
				memcpy(newdata+len_newdata, decompressed_data+i, 1);
				len_newdata++;
				for (x=0; x< width; x++)
				{
					i = len_newdata;
					memcpy(newdata+len_newdata, decompressed_data+i+2, 1);
					len_newdata++;
					memcpy(newdata+len_newdata, decompressed_data+i+1, 1);
					len_newdata++;
					memcpy(newdata+len_newdata, decompressed_data+i+0, 1);
					len_newdata++;
					memcpy(newdata+len_newdata, decompressed_data+i+3, 1);
					len_newdata++;
				}
			}

			//Compressing the image chunk
			if (chunkData)
			{
				free(chunkData);
				chunkData = NULL;
			}
			if (decompressed_data)
			{
				free(decompressed_data);
				decompressed_data = NULL;
			}


			chunkData = (char *) malloc(bufsize+1);
			memset(chunkData, 0, bufsize);
			memcpy(chunkData, newdata, bufsize);


			char *tmp = NULL;
			int output_len = -1;

			DEBUG("bufsize=%d\n", bufsize);
			tmp = compress_own(newdata, &output_len, bufsize);

			compressed_data = (char *)malloc(output_len+1);
			memset(compressed_data, 0, output_len+1);
			/*char buf[50*1024];
			memset(buf, 0, 50*1024);
			output_len = 50*1024;
			DEBUG("newdata=%p, bufsize=%d, buf=%p, output_len=%d\n", newdata, bufsize, buf, output_len);
			int ret = compress(buf, &output_len,newdata, bufsize);
			DEBUG("ret=%d, output_len=%d\n", ret, output_len);*/
			compressed_data = (char *)malloc(output_len+1);
			memset(compressed_data, 0, output_len+1);
			memcpy(compressed_data, tmp, output_len);

			memset(chunkData, 0, chunkLength);
			memcpy(chunkData, compressed_data, output_len);
			chunkLength = output_len;
			chunkCRC = crc32(0, chunkType, 4);
			chunkCRC = crc32(chunkCRC, compressed_data, output_len);
			chunkCRC = (chunkCRC + 0x100000000) % 0x100000000;

			DEBUG("output_len=%d\n", output_len);
		}

		//Removing CgBI chunk [CgBI]
		if (chunkType[0] != 'C' || chunkType[1] != 'g' ||
			chunkType[2] != 'B' || chunkType[3] != 'I')
		{

			DEBUG("This NOT CgBI chunk\n");
			int tmp;
			tmp = htonl(chunkLength);
			memcpy(pnewpng, &tmp, 4);

			memcpy(pnewpng+4, chunkType, 4);

			if (chunkLength > 0)
			{
				memcpy(pnewpng+8, chunkData, chunkLength);
			}

			tmp = htonl(chunkCRC);
			memcpy(pnewpng+chunkLength+8, &tmp, 4);

			pnewpng += chunkLength + 12;
			total_len += chunkLength + 12;

			DEBUG("total_len=%d\n", total_len);
			//if (chunkData)
				//free(chunkData);
		}
		//free data used in IDAT
		if (compressed_data)
		{
			free(compressed_data);
			compressed_data = NULL;
		}
		if (chunkData)
		{
			free(chunkData);
			chunkData = NULL;
		}
		//Stopping the PNG file parsing [IEND]
		if (chunkType[0] == 'I' && chunkType[1] == 'E' &&
			chunkType[2] == 'N' && chunkType[3] == 'D')
		{
			DEBUG("This is IEND chunk\n");
			break;
		}
		//break;
	}

	if (!strstr(filename, ".PNG") && !strstr(filename, ".png"))
	{
		char filename_png[128];
		memset(filename_png, 0, sizeof(filename_png));

		sprintf(filename_png, "%s.png", filename);
        //printf("filename_png=%s\n", filename_png);
		write_png_data(filename_png, newpng, total_len);
		return 0;
	}
    //printf("filename=%s\n", filename);
	write_png_data(filename, newpng, total_len);

    return 0;

NOT_COMPRESSED:

	if (!strstr(filename, ".PNG") && !strstr(filename, ".png"))
	{
		char filename_png[128];
		memset(filename_png, 0, sizeof(filename_png));

		sprintf(filename_png, "%s.png", filename);
        //printf("filename_png=%s\n", filename_png);
		write_png_data(filename_png, oldpng, size);
		return 0;
	}
	write_png_data(filename, oldpng, size);
	return 0;
}


//end of png normalize function


static char *base64encode(const unsigned char *buf, size_t size)
{
	if (!buf || !(size > 0)) return NULL;
	int outlen = (size / 3) * 4;
	char *outbuf = (char*)malloc(outlen+5); // 4 spare bytes + 1 for '\0'
	size_t n = 0;
	size_t m = 0;
	unsigned char input[3];
	unsigned int output[4];
	while (n < size) {
		input[0] = buf[n];
		input[1] = (n+1 < size) ? buf[n+1] : 0;
		input[2] = (n+2 < size) ? buf[n+2] : 0;
		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 3) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 15) << 2) + (input[2] >> 6);
		output[3] = input[2] & 63;
		outbuf[m++] = base64_str[(int)output[0]];
		outbuf[m++] = base64_str[(int)output[1]];
		outbuf[m++] = (n+1 < size) ? base64_str[(int)output[2]] : base64_pad;
		outbuf[m++] = (n+2 < size) ? base64_str[(int)output[3]] : base64_pad;
		n+=3;
	}
	outbuf[m] = 0; // 0-termination!
	return outbuf;
}

static void plist_node_print_to_stream(plist_t node, int* indent_level, char *stream);


/*
 60x60@2x
 icon-120
 icon120x120
*/
struct icon_array{
	int length;
	char icon[20][128];
} totalIcons;

static void plist_node_item(plist_t node, int* indent_level, int i)
{
	char *s = NULL;
	char *data = NULL;
	double d;
	uint8_t b;
	uint64_t u = 0;
	struct timeval tv = { 0, 0 };

	plist_type t;

	if (!node)
		return;

	t = plist_get_node_type(node);

	switch (t) {

	case PLIST_STRING:
		plist_get_string_val(node, &s);

		sprintf(totalIcons.icon[i], "%s", s);
		DEBUG("totalIcons=%s\n", totalIcons.icon[i]);
		free(s);
		break;
	default:
		break;
	}
}
static void plist_array_print_to_stream(plist_t node, int* indent_level, char* stream)
{
	/* iterate over items */
	int i, count;
	plist_t subnode = NULL;

	count = plist_array_get_size(node);
	totalIcons.length = count;
	for (i = 0; i < count; i++) {
		subnode = plist_array_get_item(node, i);
		//fprintf(stream, "%*s", *indent_level, "");
		//fprintf(stream, "%d: ", i);
		plist_node_item(subnode, indent_level, i);
		//DEBUG("totalIcons=%s\n", totalIcons.icon[i]);
	}

}

static void plist_dict_print_to_stream(plist_t node, int* indent_level, FILE* stream)
{
	/* iterate over key/value pairs */
	plist_dict_iter it = NULL;

	char* key = NULL;
	plist_t subnode = NULL;
	plist_dict_new_iter(node, &it);
	plist_dict_next_item(node, it, &key, &subnode);
	while (subnode)
	{
		fprintf(stream, "%*s", *indent_level, "");
		fprintf(stream, "%s", key);
		if (plist_get_node_type(subnode) == PLIST_ARRAY)
			fprintf(stream, "[%d]: ", plist_array_get_size(subnode));
		else
			fprintf(stream, ": ");
		free(key);
		key = NULL;
		plist_node_print_to_stream(subnode, indent_level, stream);
		plist_dict_next_item(node, it, &key, &subnode);
	}
	free(it);
}

static void plist_node_print_to_stream(plist_t node, int* indent_level, char *stream)
{
	char *s = NULL;
	char *data = NULL;
	double d;
	uint8_t b;
	uint64_t u = 0;
	struct timeval tv = { 0, 0 };

	plist_type t;

	if (!node)
		return;

	t = plist_get_node_type(node);

	switch (t) {
	case PLIST_BOOLEAN:
		plist_get_bool_val(node, &b);
		sprintf(stream, "%s\n", (b ? "true" : "false"));
		break;

	case PLIST_UINT:
		plist_get_uint_val(node, &u);
		sprintf(stream, "%"PRIu64"\n", u);
		break;

	case PLIST_REAL:
		plist_get_real_val(node, &d);
		sprintf(stream, "%f\n", d);
		break;

	case PLIST_STRING:
		plist_get_string_val(node, &s);

		//sprintf(totalIcons.icon[i], "%s", s);
		//printf("totalIcons=%s\n", totalIcons.icon[i]);
		//free(s);
		break;

	case PLIST_KEY:
		plist_get_key_val(node, &s);
		sprintf(stream, "%s: ", s);
		free(s);
		break;

	case PLIST_DATA:
		plist_get_data_val(node, &data, &u);
		if (u > 0) {
			s = base64encode((unsigned char*)data, u);
			free(data);
			if (s) {
				sprintf(stream, "%s\n", s);
				free(s);
			} else {
				sprintf(stream, "\n");
			}
		} else {
			sprintf(stream, "\n");
		}
		break;

	case PLIST_DATE:
		plist_get_date_val(node, (int32_t*)&tv.tv_sec, (int32_t*)&tv.tv_usec);
		{
			time_t ti = (time_t)tv.tv_sec;
			struct tm *btime = localtime(&ti);
			if (btime) {
				s = (char*)malloc(24);
 				memset(s, 0, 24);
				if (strftime(s, 24, "%Y-%m-%dT%H:%M:%SZ", btime) <= 0) {
					free (s);
					s = NULL;
				}
			}
		}
		if (s) {
			sprintf(stream, "%s\n", s);
			free(s);
		} else {
			sprintf(stream, "\n");
		}
		break;

	case PLIST_ARRAY:
		fprintf(stream, "\n");
		(*indent_level)++;
		plist_array_print_to_stream(node, indent_level, stream);
		(*indent_level)--;
		break;

	case PLIST_DICT:
		sprintf(stream, "\n");
		(*indent_level)++;
		plist_dict_print_to_stream(node, indent_level, stream);
		(*indent_level)--;
		break;

	default:
		break;
	}
}

/*void plist_print_to_stream(plist_t plist, FILE* stream)
{
	int indent = 0;

	if (!plist || !stream)
		return;

	switch (plist_get_node_type(plist)) {
	case PLIST_DICT:
		plist_dict_print_to_stream(plist, &indent, stream);
		break;
	case PLIST_ARRAY:
		plist_array_print_to_stream(plist, &indent, stream);
		break;
	default:
		plist_node_print_to_stream(plist, &indent, stream);
	}
}*/

static int zip_get_app_directory(struct zip* zf, char** path)
{
	int i = 0;
	int c = zip_get_num_files(zf);
	int len = 0;
	const char* name = NULL;

	/* look through all filenames in the archive */
	do {
		/* get filename at current index */
		name = zip_get_name(zf, i++, 0);
		if (name != NULL) {
			/* check if we have a "Payload/.../" name */
			len = strlen(name);
			if (!strncmp(name, "Payload/", 8) && (len > 8)) {
				/* locate the second directory delimiter */
				const char* p = name + 8;
				do {
					if (*p == '/') {
						break;
					}
				} while(p++ != NULL);

				/* try next entry if not found */
				if (p == NULL)
					continue;

				len = p - name + 1;

				if (*path != NULL) {
					free(*path);
					*path = NULL;
				}

				/* allocate and copy filename */
				*path = (char*)malloc(len + 1);
				strncpy(*path, name, len);

				/* add terminating null character */
				char* t = *path + len;
				*t = '\0';
				break;
			}
		}
	} while(i < c);

	return 0;
}

static int zip_get_contents(struct zip *zf, const char *filename, int locate_flags, char **buffer, uint32_t *len)
{
	struct zip_stat zs;
	struct zip_file *zfile;
	int zindex = zip_name_locate(zf, filename, locate_flags);

	*buffer = NULL;
	*len = 0;

	if (zindex < 0) {
		return -1;
	}

	zip_stat_init(&zs);

	if (zip_stat_index(zf, zindex, 0, &zs) != 0) {
		DEBUG( "ERROR: zip_stat_index '%s' failed!\n", filename);
		return -2;
	}

	if (zs.size > 10485760) {
		DEBUG( "ERROR: file '%s' is too large!\n", filename);
		return -3;
	}

	zfile = zip_fopen_index(zf, zindex, 0);
	if (!zfile) {
		DEBUG( "ERROR: zip_fopen '%s' failed!\n", filename);
		return -4;
	}

	*buffer = malloc(zs.size);
	if (zs.size > LLONG_MAX || zip_fread(zfile, *buffer, zs.size) != (zip_int64_t)zs.size) {
		DEBUG( "ERROR: zip_fread %" PRIu64 " bytes from '%s'\n", (uint64_t)zs.size, filename);
		free(*buffer);
		*buffer = NULL;
		zip_fclose(zfile);
		return -5;
	}
	*len = zs.size;
	zip_fclose(zfile);
	return 0;
}


int extract(char * file)
{
	int errp = 0;
	struct zip_file *zf= NULL;

	plist_t meta = NULL;
	//plist_t info = NULL;
	char *bundleexecutable = NULL;
	char *bundleidentifier = NULL;
	char *bundlename = NULL;
	char *bundledisplayname = NULL;
	char *bundledevregion = NULL;
	char *bundleversion = NULL;
	char *bundlever = NULL;
	int got_icon = 0;
	char fulldir[128];
    int notfoundicon = 0;
    int iconSize = 0;
    int stop_flag = 0;
    int checked = 0;

	//memset(pendingIDATChunk, 0, sizeof(pendingIDATChunk));
	//memset(newpng, 0, sizeof(newpng));
	memset(fulldir, 0, sizeof fulldir);

	zf = zip_open(file, 0, &errp);
	if (!zf){
		DEBUG( "open zip file failed, errp=%d\n", errp);
		return -1;
	}
	/* extract iTunesMetadata.plist from package */
	char *zbuf = NULL;
	uint32_t len = 0;
	plist_t meta_dict = NULL;
	if (zip_get_contents(zf, ITUNES_METADATA_PLIST_FILENAME, 0, &zbuf, &len) == 0){
		meta = plist_new_data(zbuf, len);
		if (memcmp(zbuf, "bplist00", 8) == 0){
			plist_from_bin(zbuf, len, &meta_dict);
		}
		else {
			plist_from_xml(zbuf, len, &meta_dict);
		}
	}
	else {
		DEBUG( "WARNING: could not locate %s in archive!\n", ITUNES_METADATA_PLIST_FILENAME);
	}

	if (zbuf) {
		free(zbuf);
	}

	/* determine .app directory in archive */
	zbuf = NULL;
	len = 0;
	plist_t info = NULL;
	char* filename = NULL;
	char* app_directory_name = NULL;
	int indent = 0;

	if (zip_get_app_directory(zf, &app_directory_name)) {
		DEBUG( "Unable to locate app directory in archive!\n");
		return -1;
	}

	/* construct full filename to Info.plist */
	filename = (char*)malloc(strlen(app_directory_name)+10+1);
	strcpy(filename, app_directory_name);
	sprintf(fulldir, "%s", app_directory_name);
    sprintf(absPath, "%s", app_directory_name);
	DEBUG("app_directory_name=%s\n", app_directory_name);

	//free(app_directory_name);
	app_directory_name = NULL;
	strcat(filename, "Info.plist");

	DEBUG("filename=%s\n", filename);

	if (zip_get_contents(zf, filename, 0, &zbuf, &len) < 0) {
		DEBUG( "WARNING: could not locate %s in archive!\n", filename);
		free(filename);
		zip_unchange_all(zf);
		zip_close(zf);
		zf = NULL;
		return -1;
	}
	free(filename);

	if (memcmp(zbuf, "bplist00", 8) == 0) {
		plist_from_bin(zbuf, len, &info);
	} else {
		plist_from_xml(zbuf, len, &info);
	}
	free(zbuf);

	if (!info) {
		DEBUG( "Could not parse Info.plist!\n");
		zip_unchange_all(zf);
		zip_close(zf);
		zf = NULL;
		return -2;
	}

	/* App Name */
	plist_t bname = plist_dict_get_item(info, "CFBundleExecutable");
	if (bname) {
		plist_get_string_val(bname, &bundleexecutable);
		DEBUG("CFBundleExecutable=%s\n", bundleexecutable);
	}

	/* App BundleIdentifier */
	bname = plist_dict_get_item(info, "CFBundleIdentifier");
	if (bname) {
		plist_get_string_val(bname, &bundleidentifier);
		printf("CFBundleIdentifier=%s\n", bundleidentifier);
	}

	bname = plist_dict_get_item(info, "CFBundleName");
	if (bname) {
		plist_get_string_val(bname, &bundlename);
		printf("CFBundleName=%s\n", bundlename);
	}

	bname = plist_dict_get_item(info, "CFBundleDisplayName");
	if (bname) {
		plist_get_string_val(bname, &bundledisplayname);
		printf("CFBundleDisplayName=%s\n", bundledisplayname);
	}

	bname = plist_dict_get_item(info, "CFBundleDevelopmentRegion");
	if (bname) {
		plist_get_string_val(bname, &bundledevregion);
		printf("CFBundleDevelopmentRegion=%s\n", bundledevregion);
	}

	bname = plist_dict_get_item(info, "CFBundleShortVersionString");
	if (bname) {
		plist_get_string_val(bname, &bundleversion);
		printf("CFBundleShortVersionString=%s\n", bundleversion);
	}

	bname = plist_dict_get_item(info, "CFBundleVersion");
	if (bname) {
		plist_get_string_val(bname, &bundlever);
		printf("CFBundleVersion=%s\n", bundlever);
	}

	/* App Icons CFBundleIconFiles */

	//Some apps only have CFBundleIconFiles as [dict], while others have both but may only
	//one have real icon exist, so we need to distinguish them.

	bname = plist_dict_get_item(info, "CFBundleIconFiles"); //array
	if (bname){
		switch (plist_get_node_type(bname)) {
			case PLIST_ARRAY:
				plist_array_print_to_stream(bname, &indent, bundleicons);
				break;
			default:
				DEBUG("None of them");
			}
	}

	int i = 0, desired_120p = 0;
    int selected_index = -1;//In case the file not exist, we can try to get the last index.
	for (i = 0; i< totalIcons.length; i++)
	{
		if (strstr(totalIcons.icon[i], "60x60@2x") || strstr(totalIcons.icon[i], "120"))
		{
			desired_120p = 1;
            selected_index = i;
			break;
		}
	}
    if (selected_index < 0 )
    {
        selected_index = totalIcons.length - 1;
    }

	if (desired_120p)
	{
		bundleicons = strdup(totalIcons.icon[i]);
	}
	else
	{
		bundleicons = strdup(totalIcons.icon[--i]);
	}


	char appicon[128];
	memset(appicon, 0, sizeof(appicon));
	snprintf(appicon, sizeof(appicon)-1, "%s%s", fulldir, bundleicons);
	if (bundleicons && (NULL == strstr(bundleicons, ".png") &&
		NULL == strstr(bundleicons, ".PNG")))
	{
		strcat(appicon, ".png");
	}



    DEBUG("%d: appicon = %s\n", __LINE__, appicon);

	if (zip_get_contents(zf, appicon, 0, &zbuf, &len) == 0){
		//PNG icon file
		//printf("%d, Got the icon file###############\n", __LINE__);
		//write_png_data(bundleicons, &zbuf, len);

		//newpng = (char *)malloc(len+1);
		//memset(newpng, 0, len+1);
		pngnormal(bundleicons, zbuf, len);

		if (desired_120p)
		{
			if (strstr(bundleicons, ".PNG") || strstr(bundleicons, ".png"))
			{
				printf("CFBundleIcons=%s\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
			else
			{
				printf("CFBundleIcons=%s.png\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
		}
		got_icon = 1;
	}
	else {
        DEBUG( "%d: WARNING: could not locate %s in archive!\n", __LINE__, fulldir);
	}

	if (zbuf) {
		free(zbuf);
	}

	//=============================Get BundleIcons========================
	if (!got_icon || !desired_120p)
	{
		DEBUG("Got_icon = 0, need find this one\n");
		/* CFBundlesIcons */
		bname = plist_dict_get_item(info, "CFBundleIcons"); // dict
		if (bname) {
			//printf("CFBundlePrimaryIcon Start\n");
			bname = plist_dict_get_item(bname, "CFBundlePrimaryIcon");
			bname = plist_dict_get_item(bname, "CFBundleIconFiles");
			if (bname)
			{
				switch (plist_get_node_type(bname)) {
				case PLIST_DICT:
					plist_dict_print_to_stream(bname, &indent, stdout);
					DEBUG("This type is DICT\n");
					break;
				case PLIST_ARRAY:
					plist_array_print_to_stream(bname, &indent, bundleicons);
					DEBUG("This type is ARRAY\n");
					break;
				default:
					DEBUG("None of them");
				}
				DEBUG("CFBundleIconFiles Start\n");
			}

			int j = 0, desired_120p_j = 0;
			for (j = 0; j< totalIcons.length; j++)
			{
				if (strstr(totalIcons.icon[j], "60@2x") || strstr(totalIcons.icon[j], "120"))
				{
					desired_120p_j = 1;
					selected_index = j;
					break;
				}
			}
            if (selected_index < 0 )
            {
                selected_index = totalIcons.length - 1;
            }

			//删除图标,如果在这个数组中找到了120x120像素的图片
			if (bundleicons && desired_120p_j)
			{
				DEBUG("Filename=%s\n", bundleicons);
				if (access(bundleicons, R_OK) == 0)
				{
					unlink(bundleicons);
				}
				else
				{
					char filename[128];
					memset(filename, 0, sizeof(filename));
					sprintf(filename, "%s.png", filename);
					DEBUG("Filename=%s\n", filename);
					if (access(bundleicons, R_OK) == 0)
					{
						unlink(filename);
					}
				}
				free(bundleicons);
			}
			else if(got_icon)
			{
				goto NOT_FOUND;
			}

			if (desired_120p_j)
			{
				bundleicons = strdup(totalIcons.icon[j]);
			}
			else
			{
				bundleicons = strdup(totalIcons.icon[--j]);
			}

			if (strstr(bundleicons, ".PNG") || strstr(bundleicons, ".png"))
			{
				printf("CFBundleIcons=%s\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
			else
			{
				printf("CFBundleIcons=%s.png\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
		}
		else
		{
NOT_FOUND:
			if (strstr(bundleicons, ".PNG") || strstr(bundleicons, ".png"))
			{
				printf("CFBundleIcons=%s\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
			else
			{
				printf("CFBundleIcons=%s.png\n", bundleicons);
                sprintf(iconName, "%s", bundleicons);
			}
		}

		strcat(fulldir, bundleicons);
		if (bundleicons && (NULL == strstr(bundleicons, ".png") &&
			NULL == strstr(bundleicons, ".PNG")))
		{
			strcat(fulldir, ".png");
		}
RETRY:
        DEBUG("(%d):fulldir = %s\n", __LINE__, fulldir);
		if (zip_get_contents(zf, fulldir, 0, &zbuf, &len) == 0){
			pngnormal(bundleicons, zbuf, len);
            notfoundicon = 0;
		}
		else {
            DEBUG( "%s(%d): WARNING: could not locate %s in archive!\n", __FUNCTION__, __LINE__, fulldir);
            notfoundicon = 1;
		}
        //一些App的Info.plist中并没有60x60的图标，但是存在其倍数级别的，所以这里需要手动修改其名称
        if (notfoundicon && !stop_flag)
        {
CHECK_LAST:
            if (checked == 0)
            {
                iconSize = 2;
                //AppIcon60x60 not exist; change to AppIcon60x60@2x.png
                memset(fulldir, 0, sizeof(fulldir));
                sprintf(fulldir, "%s", absPath);

                if(strstr(iconName, ".png"))
                {
                    snprintf(fulldir+strlen(fulldir),strlen(iconName)-4, "%s", iconName);
                    strcat(fulldir, "@2x.png");
                }
                else if (strstr(iconName, ".PNG"))
                {
                    snprintf(fulldir+strlen(fulldir),strlen(iconName)-4, "%s", iconName);
                    strcat(fulldir, "@2x.PNG");
                }
                else
                {
                    sprintf(fulldir+strlen(fulldir), "%s@2x.png", iconName);
                }
                checked = 1;
                DEBUG("%d: check=1\n",__LINE__);
            }
            else if (checked == 1)
            {
                iconSize = 3;
                //AppIcon60x60@2x not exist; change to AppIcon60x60@3x.png
                memset(fulldir, 0, sizeof(fulldir));
                sprintf(fulldir, "%s", absPath);

                if(strstr(iconName, ".png"))
                {
                    snprintf(fulldir+strlen(fulldir),strlen(iconName)-4, "%s", iconName);
                    strcat(fulldir, "@3x.png");
                }
                else if (strstr(iconName, ".PNG"))
                {
                    snprintf(fulldir+strlen(fulldir),strlen(iconName)-4, "%s", iconName);
                    strcat(fulldir, "@3x.PNG");
                }
                else
                {
                    sprintf(fulldir+strlen(fulldir), "%s@3x.png", iconName);
                }
                checked = 2; //skip @2x and @3x, got the else
                DEBUG("%d: check=2\n",__LINE__);
            }
            else if (checked == 2)
            {
				DEBUG("%d: check=0, selected_index=%d\n",__LINE__, selected_index);

                if (selected_index > 0)
                {
                    DEBUG("%d: iconName=%s\n", __LINE__, iconName);
                    sprintf(iconName, "%s", totalIcons.icon[selected_index-1]);
                    DEBUG("%d: iconName=%s\n", __LINE__, iconName);
                    //selected_index = -1;
                    selected_index --;
                    checked = 0; //reset the fulldir;
                    goto CHECK_LAST;
                }
                else
                {
                    stop_flag = 1;
                }
                checked = 0;
                DEBUG("%d: check=0\n",__LINE__);
            }
            if (stop_flag == 0)
                goto RETRY;
		}

		if (zbuf) {
			free(zbuf);
		}
	}

	plist_free(info);
	info = NULL;

	if (!bundleexecutable) {
		DEBUG( "Could not determine value for CFBundleExecutable!\n");
		zip_unchange_all(zf);
		zip_close(zf);
		zf = NULL;
		DEBUG( "error happened!");
	}
	if (zf)
	{
		zip_unchange_all(zf);
		zip_close(zf);
	}

EXIT:
	if (bname)
	{
		DEBUG("bname=%p\n", bname);
		free(bname);
	}
	if (app_directory_name)
	{
		DEBUG("app_directory_name=%p\n", app_directory_name);
		free(app_directory_name);
	}
	if (bundleicons)
	{
		DEBUG("bundleicons=%p\n", bundleicons);
		free(bundleicons);
	}
	/*if (bundleexecutable && zf) {
			DEBUG("zf=%p\n", zf);
			zip_unchange_all(zf);
			DEBUG("zf=%p\n", zf);
			zip_close(zf);
			DEBUG("zf=%p\n", zf);
	}*/
	if (bundleexecutable)
	{
		DEBUG("bundleexecutable=%p\n", bundleexecutable);
		free(bundleexecutable);
	}
	if (bundleidentifier){
		DEBUG("bundleidentifier=%p\n", bundleidentifier);
		free(bundleidentifier);
	}

	if (bundlename)
	{
		DEBUG("bundlename=%p\n", bundlename);
		free(bundlename);
	}
	if (bundledisplayname)
	{
		DEBUG("bundledisplayname=%p\n", bundledisplayname);
		free(bundledisplayname);
	}
	if (bundledevregion)
	{
		DEBUG("bundledevregion=%p\n", bundledevregion);
		free(bundledevregion);
	}
	if (bundleversion)
	{
		DEBUG("bundleshoartversion=%p\n", bundleversion);
		free(bundleversion);
	}
	if (bundlever)
	{
		DEBUG("bundleversion=%p\n", bundlever);
		free(bundlever);
	}
}

int main(int argc, char *argv[])
{
	if (argc <=1 || argc > 2)
	{
		printf("Usage: ./extract xxxx.ipa\n");
		return -1;
	}

	//memset(newpng, 0, sizeof(newpng));
    memset(iconName, 0, sizeof(iconName));
    memset(absPath, 0, sizeof(absPath));
	extract(argv[1]);

	return 0;

}
