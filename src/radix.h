// Data structures for an up-to-256ary tree to hold strings
//

/* forward */
struct r_node;

/* A single character to a string, a node consists of a bunch of these */

struct r_letter {
    unsigned char   c; /* The letter itself */
    struct r_node *child; /* The next node to look in after this character */

    /* Additional data to be associated with a certain string */
    /* starts here, it can be most anything. */
#ifdef COMPRESSOR


#define VALID_CODE(n) ((n) & 0x8000) ? 1 : 0)

    unsigned short code;  /* What the output code for the string up to */
                          /* this point is. */

#else
    unsigned int count;   /* How many times have we seen this string */
#endif
};


struct r_node
{
    short int       count;     // How many letters do I have.
    struct r_letter letter[1]; // An array of 'count' letters.
};

extern int r_insert(struct r_node **root, unsigned char *key);
void r_dump(struct r_node *root);

#ifdef COMPRESSOR
extern int do_compress(struct r_node *root, const unsigned char **string);
#endif

extern void init_string_compress(void);
extern int string_compress(const char *src, char *dst);
extern int string_decompress(const char *src, char *dst);
