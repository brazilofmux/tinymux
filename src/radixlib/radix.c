/*
 * Radix tree library for storing text strings
 */

#include <stdio.h>
#include "radix.h"


static int do_insert();
static void do_dump();

#ifdef COMPRESSOR
static int do_compress();
static int next_code = 1;

#endif

/*
 * Returns -1 on failure, and 0 on success (as an analyser) or the code 
 */
/*
 * assigned (as a compressor) 
 */

int r_insert(root, key)
struct r_node **root;
unsigned char *key;
{
	if (!key || !*key) {
		return -1;	/*
				 * Failure 
				 */
	}
	return do_insert(root, key);
}

#ifndef COMPRESSOR
void r_dump(root)
struct r_node *root;
{
	unsigned char buffer[4096];

	do_dump(root, buffer, 0);
}

#endif

#ifdef COMPRESSOR
int r_compress(root, string)
struct r_node *root;
unsigned char **string;
{
	return do_compress(root, string);
}
#endif


static int do_insert(curr, key)
struct r_node **curr;
unsigned char *key;
{
	int hi, lo, mid;


	if (!*key) {
		return 0;	/*
				 * end of string, we're done 
				 */
	}
	/*
	 * Find the character 'key' points at in the current node 
	 */
	/*
	 * adding it in if necessary 
	 */

	/*
	 * Empty nodes are always allocated with room for one letter 
	 */
	if ((*curr)->count == 0) {
		(**curr).letter[0].c = *key;
		(**curr).letter[0].child = NULL;
#ifdef COMPRESSOR
		(**curr).letter[0].code = 0;
#else
		(**curr).letter[0].count = 0;
#endif
		mid = 0;
		(*curr)->count = 1;
	} else {
		lo = 0;
		hi = (*curr)->count - 1;
		while (hi >= lo) {
			mid = ((hi - lo) >> 1) + lo;
			if ((**curr).letter[mid].c == *key) {
				break;
			} else if ((**curr).letter[mid].c < *key) {
				hi = mid - 1;
			} else {
				lo = mid + 1;
			}
		}
		if (hi < lo) {	/*
				 * We failed to find it, insert 
				 */
			struct r_node *tmp;

			tmp = (struct r_node *)realloc(*curr,
						     sizeof(struct r_node) +
			        (sizeof(struct r_letter) * (*curr)->count));

			if (!tmp) {
				return -1;	/*
						 * Failure 
						 */
			}
			*curr = tmp;
			(*curr)->count += 1;
			if (lo + 1 < (*curr)->count) {
				bcopy(&((*curr)->letter[lo]),
				      &((*curr)->letter[lo + 1]),
				      ((*curr)->count - (lo + 1)) *
				      sizeof(struct r_letter));
			}
			(*curr)->letter[lo].c = *key;
			(*curr)->letter[lo].child = NULL;
#ifdef COMPRESSOR
			(*curr)->letter[lo].code = 0;
#else
			(*curr)->letter[lo].count = 0;
#endif
			mid = lo;
		} else {
		}
	}

	/*
	 * If we get here, mid is the index of the right character, so 
	 */
	/*
	 * update it and recurse as needed 
	 */

#ifndef COMPRESSOR
	(*curr)->letter[mid].count += 1;
#endif

	if (key[1] == '\0') {
#ifdef COMPRESSOR
		(*curr)->letter[mid].code = next_code;
		return next_code++;	/*
					 * success 
					 */
#else
		return 0;
#endif
	}
	/*
	 * We have more string to go, so carry on 
	 */
	if ((*curr)->letter[mid].child == NULL) {
		struct r_node *tmp;

		tmp = (struct r_node *)malloc(sizeof(struct r_node));

		if (!tmp) {
			return -1;
		}
		tmp->count = 0;
		(*curr)->letter[mid].child = tmp;
	}
	return do_insert(&((*curr)->letter[mid].child), key + 1);
}


#ifdef COMPRESSOR
static int do_compress(curr, str)
struct r_node *curr;
unsigned char **str;
{
	int hi, lo, mid, code;

	if (!*str) {
		return 0;	/*
				 * end of string, we're done 
				 */
	}
	/*
	 * Find the character 'key' points at in the current node 
	 */
	/*
	 * adding it in if necessary 
	 */

	if (curr == NULL || curr->count == 0) {
		/*
		 * Can't code the next letter, return 0 
		 */
		return 0;
	}
	lo = 0;
	hi = curr->count - 1;
	while (hi >= lo) {
		mid = ((hi - lo) >> 1) + lo;
		if (curr->letter[mid].c == **str) {
			/*
			 * See if we can go further 
			 */

			(*str)++;
			code = do_compress(curr->letter[mid].child, str);
			if (code == 0) {
				if (curr->letter[mid].code) {
					return curr->letter[mid].code;
				} else {
					(*str)--;
					return 0;
				}
			} else {
				return code;
			}
		} else if (curr->letter[mid].c < **str) {
			hi = mid - 1;
		} else {
			lo = mid + 1;
		}
	}

	/*
	 * Failed to code this letter, return 0 
	 */

	return 0;
}
#endif

#ifndef COMPRESSOR
static void do_dump(curr, buff, depth)
struct r_node *curr;
unsigned char *buff;
int depth;
{
	int i;

	if (!curr)
		return;

	for (i = 0; i < curr->count; i++) {
		/*
		 * Emit the current string 
		 */
		buff[depth] = curr->letter[i].c;
		buff[depth + 1] = '\0';
#ifdef COMPRESSOR
		printf("%d '%s'\n", curr->letter[i].code, buff);
#else
		printf("%d '%s'\n", curr->letter[i].count, buff);
#endif

		/*
		 * Recurse to handle all extensions of this string too 
		 */

		do_dump(curr->letter[i].child, buff, depth + 1);
	}
}
#endif
