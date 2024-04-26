/**
 * req.c
 * -------
 *
 * A test client for (selectively) stressing out lux.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <curl/curl.h>

#define NAME "req"

#define MAX_THREADCOUNT 9999

#define LOGFMT "REQ %04d (%p) = Time: %0.2fs, Size: %ldb, Status: %d, Url: %s\n"

#define HELP \
"-F, -file <arg>             Use file <arg> as the comparison file.\n" \
"-S, -save                   Save the payload with a sensible suffix.\n" \
"-P, -path <arg>             Define an alternate path to save files (default is the current directory).\n" \
"-u, -url <arg>              Fetch URL <arg>.\n" \
"-X, -stats                  Display statistics for the current system's memory usage.\n" \
"-t, -threads <arg>          Start <arg> threads (current max %d)\n" \
"-h, -help                   Show help and quit.\n"


typedef enum {
	ERX_NONE = 0,
	ERX_PTHREAD_CREATE,
	ERX_TRANSFER_FAILED,
	ERX_FILESAVE_FAILED,
} error_t;


static const char *errors[] = {
	[ ERX_NONE            ] = "No errors.",
	[ ERX_PTHREAD_CREATE  ] = "pthread create() failed.",
	[ ERX_TRANSFER_FAILED ] = "File transfer failed.",
	[ ERX_FILESAVE_FAILED ] = "File save failed.",
};


static const char *useragents[] = {
	"Yer_Mommas_Favorite_Client/v1.1"
};


/**
 * typedef struct req_t 
 *
 * Request structure for reading off data from curl within a thread.
 *
 */
typedef struct req_t {
	pthread_t threadid;
	size_t size;
	char *filename;
	error_t error;
	double ttime;
	unsigned short status;
	unsigned int tspeedavg;
	char *url;
	char header[ 16 ];
	int fd;
#if 0
	char *unused;
	char *stillunused;
#endif
} req_t;




/**
 * typedef struct config_t 
 *
 * Configuration data structure for CLI users.
 *
 */
typedef struct config_t {
	char *file;
	int save;
	char *url;
	char *path;
	int threadcount;
	int stats;
} config_t;

config_t config = {0};



/**
 * static size_t header_callback ( char *buffer, size_t size, size_t nmemb, void *ud ) 
 *
 * Try to extract that status code...
 *
 */
static size_t header_callback ( char *buffer, size_t size, size_t nmemb, void *ud ) {

	// 
	int sz = 0;	
	req_t *t = (req_t *)ud;
	char *sb = NULL;

	#if 0
	// Keep this should you need to do more complicated header processing in the future
	memcpy( header, buffer, len );
	write( 2, header, len );
	#endif

	// Extract JUST THE STATUS
	if ( t->status == 999 && ( sb = memchr( buffer, ' ', nmemb * size ) ) ) {
		char stat[ 4 ] = {0};
		memcpy( stat, ++sb, 3 );
		t->status = atoi( stat );
	}

	// This should result in success...
	return size * nmemb;
}



/**
 * static size_t size_tracker_callback ( void *c, size_t size, size_t nmemb, void *ud ) 
 *
 * Tracks the final size of a request payload.
 *
 */
static size_t size_tracker_callback ( void *c, size_t size, size_t nmemb, void *ud ) {
	size_t realsize = size * nmemb;
	req_t *t = (req_t *)ud;
	t->size += realsize;
	if ( config.save && ( t->fd > -1 ) ) {
		write( t->fd, c, realsize ); 
	}
	return realsize;
}



/**
 * void * connection ( void *p ) 
 *
 * Creates a new connection w/ Curl.
 *
 */
void * connection ( void *p ) {
	CURL *curl = NULL;
	CURLcode res;
	req_t *t = (req_t *)p;
	char fname[ PATH_MAX ] = {0};

	// Initialize the header space (TODO: way less is needed)
	memset( t->header, 0, sizeof( t->header ) );

	// If the user requested the save option, open a file
	if ( config.save ) {
		snprintf( fname, PATH_MAX - 1, "%s/%s-%p", config.path, "file", (void *)t );
		//fprintf( stderr, "Attempting to save to %s\n", fname );
		if ( ( t->fd = open( fname, O_CREAT | O_TRUNC | O_WRONLY, 0644 ) ) == -1 ) {
			t->error = ERX_FILESAVE_FAILED;
			//fprintf( stderr, "Error %s\n", strerror( errno ) );
			return t;
		}
	}

	// Initialize a cURL instance
	if ( !( curl = curl_easy_init() ) ) {
		t->error = ERX_PTHREAD_CREATE;
		return t;
	}

	// Set as many options as possible
	curl_easy_setopt( curl, CURLOPT_URL, t->url );
	curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, header_callback );
	curl_easy_setopt( curl, CURLOPT_HEADERDATA, (void *)t );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, size_tracker_callback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *)t );
#if 0
	// Optionally define a different user-agent
	curl_easy_setopt( curl, CURLOPT_USERAGENT, "My Custom User-Agent" );

	// Optionally save the content-type of the request
	curl_easy_setopt( curl, CURLOPT_CONTENT_TYPE, &t->ctype );
#endif

	// What are the other errors that can be returned...?
	if ( ( res = curl_easy_perform( curl ) ) != CURLE_OK ) {
		fprintf( stderr, NAME ": URL transfer failed %s\n", "*" );
		t->error = ERX_TRANSFER_FAILED;
		curl_easy_cleanup( curl );
		return t;
	}

	// Log the time if possible
	curl_easy_getinfo( curl, CURLINFO_TOTAL_TIME, &t->ttime );
	curl_easy_cleanup( curl );

	// Close any open file
	if ( config.save ) {
		close( t->fd );
	}
	return t;
}



/**
 * int main (int argc, char *argv[]) 
 *
 * The test program.
 *
 */
int main (int argc, char *argv[]) {

	if ( argc < 2 ) {
		fprintf( stderr, "Usage: %s\n", *argv );
		fprintf( stderr, HELP, MAX_THREADCOUNT );
		return 1;
	}

	// Set defaults here until we clean up the config structure
	config.path = ".";

	for ( ++argv; *argv; ) {
		if ( !strcmp( *argv, "-t" ) || !strcmp( *argv, "-threads" ) )
			config.threadcount = atoi( *(++argv) );
		else if ( !strcmp( *argv, "-u" )  || !strcmp( *argv, "-url" ) )
			config.url = *(++argv);	
		else if ( !strcmp( *argv, "-S" )  || !strcmp( *argv, "-save" ) )
			config.save = 1;
		else if ( !strcmp( *argv, "-F" )  || !strcmp( *argv, "-file" ) )
			config.file = *(++argv);
		else if ( !strcmp( *argv, "-X" )  || !strcmp( *argv, "-stats" ) )
			config.stats = 1;
		else if ( !strcmp( *argv, "-P" )  || !strcmp( *argv, "-path" ) )
			config.path = *(++argv);
		else {
			fprintf( stderr, "Unknown arg: %s\n", *argv );
			return 1;
		}
		argv++;
	}

	if ( config.threadcount < 1 || config.threadcount > MAX_THREADCOUNT ) {
		fprintf( stderr, NAME ": Invalid -threadcount value => %d\n", config.threadcount );
		return 1;
	}

	if ( curl_global_init( CURL_GLOBAL_ALL ) != 0 ) {
		fprintf( stderr, NAME ": Global init failed %s\n", "*" );
		return 1;
	}

	if ( config.stats ) {
		const char *fmt = "Test suite will use: %ld kb of memory\n";
		fprintf( stdout, fmt, ( sizeof( req_t ) * 100000 ) / 1024 );
	}

	if ( strlen( config.path ) > 2 && config.path[0] != '.' && config.path[1] != '.' ) {
		struct stat sb;
		memset( &sb, 0, sizeof( struct stat ) );
		if ( stat( config.path, &sb ) == -1 && mkdir( config.path, 0755 ) == -1 ) {
			fprintf( stderr, NAME ": Directory %s inaccessible...\n", config.path );
			return 1;
		}
	}

	// TODO: This should be allocated ahead of time...
	pthread_t threads[ config.threadcount ];
	memset( threads, 0, sizeof( pthread_t ) * config.threadcount );
	req_t *reqs[ config.threadcount ];
	memset( threads, 0, sizeof( req_t * ) * config.threadcount );

	// Add a timer. Pretty rare you won't want this...
	struct timespec tstart, estart;
	memset( &tstart, 0, sizeof( struct timespec ) );
	clock_gettime( CLOCK_REALTIME, &tstart );

	// Making requests to 
	fprintf( stdout, "Making requests to %s\n", config.url );

	// Allocate all the structures ahead of time
	for ( int i = 0; i < config.threadcount; i++ ) {
		reqs[ i ] = malloc( sizeof( req_t ) );		
		memset( reqs[ i ], 0, sizeof( req_t ) );
	}	

	// Make all of the requests
	for ( int i = 0; i < config.threadcount; i++ ) {
		req_t *t = reqs[ i ];
		t->url = config.url;
		t->size = 0;
		t->fd = -1;
		t->status = 999;

		if ( pthread_create( &threads[ i ], NULL, connection, t ) != 0 ) {
			fprintf( stderr, "pthread_create unsuccessful: %s\n", strerror( errno ) );
			return 1;
		}

		t->threadid = threads[ i ];
	}

	// "Reap" all the threads
	for ( int i = 0; i < config.threadcount; i++ ) {
		if ( pthread_join( threads[ i ], NULL ) > 0 ) {
			fprintf( stderr, "pthread_join unsuccessful: %s\n", strerror( errno ) );
			return 1;
		}
	}

	// Mark the end
	clock_gettime( CLOCK_REALTIME, &estart );
	int elapsed = estart.tv_sec - tstart.tv_sec;

	// Print this
	fprintf( stdout, "Time elapsed: ~%dm%ds\n", elapsed / 60, elapsed % 60 );
			

	// Give a status report on all of them
	for ( int i = 0; i < config.threadcount; i++ ) {
		req_t *t = reqs[ i ];
		if ( !t->error ) {
			fprintf( stdout, LOGFMT, i, (void *)t, t->ttime, t->size, t->status, t->url );
		}
		else {
			fprintf( stdout, "REQ %4d = Error: %s\n", i /*, t->threadid*/, errors[ t->error ] );
		}
		free( reqs[ i ] );
	}

	return 0;
}
