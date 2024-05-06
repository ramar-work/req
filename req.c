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
#include <sys/resource.h>
#include <sys/mman.h>

#define NAME "req"

#define MAX_THREADCOUNT 9999

#define LOGFMT "REQ %04d (%p) = Time: %0.2fs, Size: %ldb, Status: %d, %s\n"

#define HELP \
	"-F, -file <arg>             Use file <arg> as the comparison file.\n" \
	"-S, -save                   Save the payload with a sensible suffix.\n" \
	"-P, -path <arg>             Define an alternate path to save files (default is the current directory).\n" \
	"-u, -url <arg>              Fetch URL <arg>.\n" \
	"-X, -stats                  Display statistics for the current system's memory usage.\n" \
	"-t, -threads <arg>          Start <arg> threads (current max %d)\n" \
	"-V, -verify <arg>           Verify the payload received\n" \
	"-K, -maxstacksize <arg>     Specify the maximum stack size\n" \
	"-O, -maxopenfiles <arg>     Specify the maximum open file count\n" \
	"-h, -help                   Show help and quit.\n"


typedef enum {
	ERX_NONE = 0,
	ERX_CURL_CREATE,
	ERX_TRANSFER_FAILED,
	ERX_FILESAVE_FAILED,
} error_t;


static const char *errors[] = {
	[ ERX_NONE            ] = "No errors.",
	[ ERX_CURL_CREATE     ] = "curl_easy_init() failed.",
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
	char header[ 16 ];
	int fd;
	char errmsg[ 128 ]; 
	int cfd;
	int coffset;
	int checksum;
	unsigned char *cmap;
	ssize_t cmaplen;
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
	int ufile;
	int save;
	char *url;
	int uurl;
	char *path;
	int upath;
	char *verify;
	int uverify;
	int threadcount;
	int uthreadcount;
	int stats;
	long stacksize;
	long openfiles;
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
#if 0
	if ( t->cfd > -1 ) {
		// Slow, but copying to buffer is the best we can do...
		unsigned char buf[ realsize ];
		memset( buf, 0, realsize );
		read( t->cfd, buf, realsize );
		t->coffset += realsize;
		lseek( t->cfd, t->coffset, SEEK_SET ); 
		if ( memcmp( c, buf, realsize ) != 0 ) {
			t->checksum = 0;
		}
	}
#endif
	if ( t->cmap && t->cmaplen >= realsize ) {
		if ( memcmp( c, t->cmap, realsize ) != 0 ) {
			t->checksum = 0;
		}
		t->cmap += realsize;
		t->cmaplen -= realsize;
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
	char fname[ 256 ] = {0};

	// Initialize the header space (TODO: way less is needed)
	memset( t->header, 0, sizeof( t->header ) );

	// If the user requested the save option, open a file
	if ( config.save ) {
		// Shut down if the filename is too long 
		snprintf( fname, 255, "%s/%s-%p", config.path, "file", (void *)t );
		//fprintf( stderr, "Attempting to save to %s\n", fname );
		if ( ( t->fd = open( fname, O_CREAT | O_TRUNC | O_WRONLY, 0644 ) ) == -1 ) {
			t->error = ERX_FILESAVE_FAILED;
			snprintf( t->errmsg, sizeof( t->errmsg ) - 1, "%s: %s", errors[ ERX_FILESAVE_FAILED ], strerror( errno ) );
			//fprintf( stderr, "Error %s\n", strerror( errno ) );
			return t;
		}
	}

#if 0
	// If we want to check that the bytes match, set checksum here
	if ( t->cfd > -1 ) {
		t->checksum = 1;
	}
#else
	if ( t->cmap ) {
		t->checksum = 1;
	}
#endif

	// Initialize a cURL instance
	if ( !( curl = curl_easy_init() ) ) {
		t->error = ERX_CURL_CREATE;
		snprintf( t->errmsg, sizeof( t->errmsg ) - 1, "%s: %s", errors[ ERX_CURL_CREATE ], strerror( errno ) );
		return t;
	}

	// Set as many options as possible
	curl_easy_setopt( curl, CURLOPT_URL, config.url );
	curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, header_callback );
	curl_easy_setopt( curl, CURLOPT_HEADERDATA, (void *)t );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, size_tracker_callback );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *)t );
	curl_easy_setopt( curl, CURLOPT_ERRORBUFFER, t->errmsg );
#if 0
	// Optionally define a different user-agent
	curl_easy_setopt( curl, CURLOPT_USERAGENT, "My Custom User-Agent" );

	// Optionally save the content-type of the request
	curl_easy_setopt( curl, CURLOPT_CONTENT_TYPE, &t->ctype );
#endif

	// What are the other errors that can be returned...?
	if ( ( res = curl_easy_perform( curl ) ) != CURLE_OK ) {
		//fprintf( stderr, NAME ": URL transfer failed %s\n", "*" );
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

#if 0
	if ( t->cfd > -1 ) {
		close( t->cfd );
	}
#endif
	return t;
}



/**
 * int main (int argc, char *argv[]) 
 *
 * The test program.
 *
 */
int main (int argc, char *argv[]) {

	// Defining a few necessary things at the top
	const unsigned char *cmap = NULL; 
	unsigned int cmaplen = 0;
	int cmapfd = -1;

	if ( argc < 2 ) {
		fprintf( stderr, "Usage: %s\n", *argv );
		fprintf( stderr, HELP, MAX_THREADCOUNT );
		return 1;
	}

	// Set defaults here until we clean up the config structure
	config.path = ".";

	for ( ++argv; *argv; ) {
		if ( !strcmp( *argv, "-t" ) || !strcmp( *argv, "-threads" ) )
			config.threadcount = atoi( *(++argv) ), config.uthreadcount = 1;
		else if ( !strcmp( *argv, "-u" )  || !strcmp( *argv, "-url" ) )
			config.url = *(++argv), config.uurl = 1;	
		else if ( !strcmp( *argv, "-S" )  || !strcmp( *argv, "-save" ) )
			config.save = 1;
		else if ( !strcmp( *argv, "-F" )  || !strcmp( *argv, "-file" ) )
			config.file = *(++argv), config.ufile = 1;
		else if ( !strcmp( *argv, "-X" )  || !strcmp( *argv, "-stats" ) )
			config.stats = 1;
		else if ( !strcmp( *argv, "-P" )  || !strcmp( *argv, "-path" ) )
			config.path = *(++argv), config.upath = 1;
		else if ( !strcmp( *argv, "-V" )  || !strcmp( *argv, "-verify" ) )
			config.verify = *(++argv), config.uverify = 1;
		else if ( !strcmp( *argv, "-K" )  || !strcmp( *argv, "-maxstacksize" ) )
			config.stacksize = atol( *(++argv) );
		else if ( !strcmp( *argv, "-O" )  || !strcmp( *argv, "-openfiles" ) )
			config.openfiles = atol( *(++argv) );
		else {
			fprintf( stderr, "%s: Got unknown arg: %s\n", NAME, *argv );
			return 1;
		}
		argv++;
	}

#if 0
	// Check arguments from here...
	if ( config.uthreadcount && config.threadcount < 1 ) {
		fprintf( stderr, "-threads flag specified, but got no argument.\n" );	
		return 1;
	}
		
	if ( config.uurl && !config.url ) {
		fprintf( stderr, "-url flag specified, but got no argument.\n" );	
		return 1;
	}
		
	if ( config.ufile && !config.file ) {
		fprintf( stderr, "-file flag specified, but got no argument.\n" );	
		return 1;
	}
		
	if ( config.upath && !config.path ) {
		fprintf( stderr, "-path flag specified, but got no argument.\n" );	
		return 1;
	}
		
	if ( config.uverify && !config.verify ) {
		fprintf( stderr, "-verify flag specified, but got no argument.\n" );	
		return 1;
	}

	if ( config.threadcount < 1 || config.threadcount > MAX_THREADCOUNT ) {
		fprintf( stderr, NAME ": Invalid -threadcount value => %d\n", config.threadcount );
		return 1;
	}
#endif

	if ( config.stacksize != 0 ) {
		struct rlimit r = {0};
		getrlimit( RLIMIT_STACK, &r );
		fprintf( stderr, "Stack size max of current process is: %lu\n", r.rlim_cur );
		if ( config.stacksize < 0 || config.stacksize > r.rlim_max ) {
			fprintf( stderr, "Specified maximum stack size is invalid: %ld\n", config.stacksize );
			return 1;
		}
		r.rlim_cur = (unsigned long)config.stacksize;
		if ( setrlimit( RLIMIT_STACK, &r ) == -1 ) {
			fprintf( stderr, "Error setting maximum stack size: %s\n", strerror( errno ) );
			return 1;
		}
	}

	if ( config.openfiles != 0 ) {
		struct rlimit r = {0};
		getrlimit( RLIMIT_NOFILE, &r );
		fprintf( stderr, "Max. open files of current process is: %lu\n", r.rlim_cur );
		if ( config.openfiles < 0 || config.openfiles > r.rlim_max ) {
			fprintf( stderr, "Specified maximum open file count is invalid: %ld > %lu\n", config.openfiles, r.rlim_max );
			return 1;
		}
		r.rlim_cur = (unsigned long)config.openfiles;
		if ( config.openfiles > r.rlim_cur && setrlimit( RLIMIT_NOFILE, &r ) == -1 ) {
			fprintf( stderr, "Error increasing open files limit: %s\n", strerror( errno ) );
			return 1;
		}
	}

	if ( curl_global_init( CURL_GLOBAL_ALL ) != 0 ) {
		fprintf( stderr, NAME ": Global init failed %s\n", "*" );
		return 1;
	}

	if ( strlen( config.path ) > 2 && config.path[0] != '.' && config.path[1] != '.' ) {
		struct stat sb = {0};
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
	if ( config.stats ) {
		// TODO: Need to add the thread usage space to this...
		const char *fmt = "Test suite will use: %ld kb of memory\n";
		fprintf( stdout, "Making requests to [%s]\n", config.url );
		fprintf( stdout, fmt, ( sizeof( req_t ) * 100000 ) / 1024 );
	}

	// Allocate all the structures ahead of time
	for ( int i = 0; i < config.threadcount; i++ ) {
		reqs[ i ] = malloc( sizeof( req_t ) );		
		memset( reqs[ i ], 0, sizeof( req_t ) );
	}	


	// If we requested a verification file, use mmap and load it once
	if ( config.verify ) {	
		struct stat sb = {0};

		if ( stat( config.verify, &sb ) == -1 ) {
			fprintf( stderr, NAME ": Directory %s inaccessible...\n", config.path );
			return 1;
		}

		if ( ( cmaplen = sb.st_size ) == 0 ) {
			fprintf( stderr, NAME ": Size of verification file is zero-length...\n" );
			return 1;
		}

		if ( ( cmapfd = open( config.verify, O_RDONLY ) ) == -1 ) {
			fprintf( stderr, NAME ": Error opening verification file: %s: %s...\n", 
				config.verify, strerror( errno ) );
			return 1;
		}

		if ( ( cmap = mmap( 0, sb.st_size, PROT_READ, MAP_PRIVATE, cmapfd, 0 ) ) == MAP_FAILED ) {
			fprintf( stderr, "Error mapping verification file: %s\n", strerror( errno ) );
			return 1;
		}
	}

	// Make all of the requests
	for ( int i = 0; i < config.threadcount; i++ ) {
		req_t *t = reqs[ i ];
		t->size = 0;
		t->fd = -1;
		t->status = 999;
		t->cfd = -1;
		t->checksum = -1;
		t->coffset = 0;
		t->cmap = (unsigned char *)cmap;
		t->cmaplen = cmaplen;
#if 0
		pthread_attr_t tattr = {0};
		pthread_attr_init( &tattr )	

		// Try opening the comparison file here
		if ( config.verify ) {
			// TODO: Let me know the file is open in verbose mode at least once...
			if ( ( t->cfd = open( config.verify, O_RDONLY ) ) == -1 ) {
				fprintf( stderr, "verification file inaccessible: %s\n", strerror( errno ) );
				return 1;
			}
		}
#endif

		if ( pthread_create( &threads[ i ], NULL, connection, t ) != 0 ) {
			fprintf( stderr, "pthread_create unsuccessful: %s\n", strerror( errno ) );
			// We've hit a limit.  I suggest stopping, but you'd have to reap everything...
			//return 1;
			// could always just exit and let the OS deal with it...
			exit( 1 );
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

	// Unmap the block if we asked for verification
	if ( cmap ) {
		munmap( (void *)cmap, cmaplen );
		close( cmapfd );
	}

	// Mark the end
	clock_gettime( CLOCK_REALTIME, &estart );
	int elapsed = estart.tv_sec - tstart.tv_sec;

	// Print this
	fprintf( stdout, "Time elapsed: ~%dm%ds\n", elapsed / 60, elapsed % 60 );
	
	// Get the fastest and slowest requests and average time of requests 		
	int ftime = (double)INT_MAX, stime = 0; 
	int ccount = 0; 
	double avgtime = 0.0, total = 0.0;

	for ( int i = 0; i < config.threadcount; i++ ) {
		req_t *t = reqs[ i ];
		if ( !t->error ) {
			// Add to the total and increase completed client count
			total += t->ttime, ccount ++;
		
			// Check for the fastest	
			if ( t->ttime < ftime ) {
				ftime = t->ttime;	
			}

			// Check for the slowest 
			if ( t->ttime > stime ) {
				stime = t->ttime;	
			}
		}
	}

	// Print this
	fprintf( stdout, "%d/%d requests completed\n", ccount, config.threadcount );
	//fprintf( stdout, "Average time to complete: ~%dm%0.2fs\n", ( avgtime / ccount ) / 60, (double)(( avgtime / ccount ) % 60)  );
	fprintf( stdout, "Fastest request: ~%dm%ds\n", ftime / 60, ftime % 60 );
	fprintf( stdout, "Slowest request: ~%dm%ds\n", stime / 60, stime % 60 );

	// Give a status report on all of them
	for ( int i = 0; i < config.threadcount; i++ ) {
		req_t *t = reqs[ i ];
		if ( !t->error ) {
			fprintf( stdout, LOGFMT, i, (void *)t, t->ttime, t->size, t->status, 
				( t->checksum == -1 ) ? "-" : ( ( t->checksum ) ? "S" : "F"  ) );
		}
		else {
			fprintf( stdout, "REQ %4d (%p) = Error: %s\n", i, (void *)t, t->errmsg );
		}
		free( reqs[ i ] );
	}

	return 0;
}
