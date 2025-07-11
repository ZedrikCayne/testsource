#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include <crankshaft/tempbuff.h>
#include <crankshaft/alloc.h>
#include <crankshaft/thread.h>
#include <crankshaft/pushpull.h>
#include <crankshaft/logger.h>
#include <crankshaft/socket.h>
#include <crankshaft/ssl.h>
#include <crankshaft/list.h>
#include <crankshaft/util.h>
#include <crankshaft/random.h>

static char *wordBuffer = NULL;
static char **wordsArray = NULL;
static int numWords = 0;

struct threadContext {
    struct CS_Socket *socket;
    struct CS_Thread *consumerThread;
    int miliseconds;
    int size;
    int currentIndex;
    int number;
    struct CS_LCG_rand_state state;
};

static char *getWord(struct threadContext *tc) {
    return wordsArray[ ((CS_LCG_rand(&tc->state)&0x7FFFFFFF) % numWords) ];
}

struct commandHandler {
    const char *what;
    int length; 
    void (*handle)(struct threadContext *tc, char *text, int length);
};

static int getAnInteger( char *line, int length ) {
    char *storage;
    strtok_r( line, " ", &storage );
    char *amount = strtok_r( NULL, " ", &storage );
    if( !amount ) return -1;
    return strtol(amount,NULL,10);
}
static void doTime( struct threadContext *tc, char *line, int length ) {
    CS_LOG_LOUD("Time");
    int anInteger = getAnInteger(line,length);
    if( anInteger < 1 ) return;
    tc->miliseconds = anInteger;
    CS_LOG_LOUD("Num miliseconds %d", tc->miliseconds);
}
static void doSize( struct threadContext *tc, char *line, int length ) {
    CS_LOG_LOUD("Size:");
    int anInteger = getAnInteger(line,length);
    if( anInteger < 1 ) return;
    tc->size = anInteger;
    CS_LOG_LOUD("Size: %d", tc->size);
}
static void doNumber( struct threadContext *tc, char *line, int length ) {
    CS_LOG_LOUD("Number:");
    int anInteger = getAnInteger(line,length);
    if( anInteger < 1 ) return;
    tc->number = anInteger;
    CS_LOG_LOUD("Number: %d", tc->size);
}
static void doDisconnect( struct threadContext *tc, char *line, int length ) {
}

struct commandHandler handlers[] = {
    {"time", 4, doTime},
    {"size", 4, doSize},
    {"number", 6, doNumber},
    {"dc", 2, doDisconnect }
};

static bool consumerThread( struct CS_Thread *inThread, int threadState, void *context) {
    struct threadContext *tc = (struct threadContext *)context;
    struct CS_Socket *socket = (struct CS_Socket *)tc->socket;
    
    if( threadState == CS_THREAD_RUNNING ) {
        struct CS_PushPullBuffer * ppIn = CS_socketLockInputBuffer( socket );
        int numBytesRead = CS_socketFillIncomingBuffer( socket, false );
        if( numBytesRead < 0 ) {
            CS_socketUnlockInputBuffer( socket );
            return true;
        }
        if( numBytesRead > 0 ) {
            for( int i = 0; i < CS_ARRAY_SIZE( handlers ); ++i ) {
                if( strncmp( CS_PP_startOfData(ppIn), handlers[ i ].what, handlers[i].length ) == 0 ) {
                    handlers[i].handle( tc, CS_PP_startOfData( ppIn ), CS_PP_dataSize( ppIn ) );
                }
            }
            CS_PP_reset( ppIn );
        }
        CS_socketUnlockInputBuffer( socket );
    }
    return false;
}

static void sleepMiliseconds( int nMiliseconds ) {
    int nSeconds = nMiliseconds / 1000;
    int nanoSeconds = (nMiliseconds % 1000) * 1000000;
    struct timespec ts = {nSeconds, nanoSeconds};
    nanosleep(&ts,NULL);
}

static bool providerThread( struct CS_Thread *inThread, int threadState, struct CS_Socket *socket ) {
    struct threadContext *tc = (struct threadContext *)CS_socketGetContext( socket );
    if( threadState == CS_THREAD_STOP ) {
        if( tc ) {
            if( tc->consumerThread ) CS_threadStop( tc->consumerThread );
            CS_socketClose( socket );
            CS_free(tc);
        }
    } else if( threadState == CS_THREAD_START ) {
        tc = (struct threadContext *)CS_alloc( sizeof(struct threadContext) );
        tc->socket = socket;
        tc->currentIndex = 0;
        tc->miliseconds = 1000;
        tc->size = 128;
        tc->number = 1;
        tc->state.seed = 78234;
        tc->consumerThread = CS_threadStart( "consumer", tc, consumerThread );
        if( tc->consumerThread == NULL ) {
            CS_free(tc);
            return true;
        }
        CS_socketPutContext( socket, tc );
    } else if ( threadState == CS_THREAD_RUNNING ) {
        sleepMiliseconds( tc->miliseconds );

        struct CS_PushPullBuffer *pp = CS_socketLockOutputBuffer( socket );
        for( int i = 0; i < tc->number; ++i ) {
            int randNewSize = ((CS_rand()&0x7FFFFFFF) % tc->size);
            tc->currentIndex = tc->currentIndex + 1;
            CS_PP_printf( pp, "%d %s", tc->currentIndex, getWord(tc) );

            while( CS_PP_dataSize( pp ) < randNewSize && CS_PP_bufferRemaining( pp ) > tc->size ) {
                //CS_PP_printf( pp, " %s", getWord(tc) ); 
                CS_PP_printf( pp, " %d %s", tc->currentIndex, getWord(tc) );
            }
            CS_PP_printf( pp, ".\r\n" );
        }
        while( CS_PP_dataSize( pp ) > 0 ) {
            if( CS_socketEmptyOutputBuffer(socket, false) < 0 ) {
                CS_socketUnlockOutputBuffer( socket );
                return true;
            }
            sleepMiliseconds( 30 );
        };
        CS_socketUnlockOutputBuffer( socket );
    }
    return false;
}

int main( int argc, char **argv ) {
    CS_tempAllocateGlobal( 200000 );
    CS_sslInit(NULL,NULL,"localhost");
    CS_LOG_VERBOSE_BOOL=true ; CS_LOG_INFO_BOOL=true ; CS_LOG_QUIET_BOOL=false; CS_LOG_TRACE_BOOL=true ; CS_LOG_WARN_BOOL=true; CS_LOG_ERROR_BOOL=true;
    struct CS_List *wordList = CS_listCreate( 32768 );

    if( !wordList ) {
        CS_LOG_ERROR("Could not create word list from /usr/share/dict/words");
        return 1;
    }

    int dictBytes = 0;
    wordBuffer = (char*)CS_utilLoadWholeFile( "/usr/share/dict/words", &dictBytes );
    if( !wordBuffer ) {
        CS_LOG_ERROR("Could not load word list from /usr/share/dict/words");
        return 1;
    }
    wordBuffer = (char*)CS_realloc( wordBuffer, dictBytes + 5 );
    if( !wordBuffer ) {
        CS_LOG_ERROR("Could not realloc the word list so we can null terminate it.");
        return 1;
    }
    wordBuffer[ dictBytes ] = 0;
    bool whitespace = true;
    for( int i = 0; i < dictBytes; ++i ) {
        if( whitespace ) {
            if( isspace( wordBuffer[i] ) || !wordBuffer[i] ) {
                wordBuffer[i] = 0;
            } else {
                CS_listPushTail( wordList, wordBuffer + i, 0 );
                whitespace = false;
            }
        } else {
            if( isspace( wordBuffer[i] ) || !wordBuffer[i] ) {
                wordBuffer[i] = 0;
                whitespace = true;
            }
        }
    }

    numWords = CS_listCount( wordList );

    wordsArray = (char**)CS_alloc( numWords * sizeof( char * ) );
    if( wordsArray == NULL ) {
        CS_LOG_ERROR("Could not allocate the words for the array.");
        return 1;
    }
    char **wordArrayIter = wordsArray;

    CS_LOG_LOUD("We have %d words\n", numWords );

    CS_LIST_ITER( wordList, wordItem ) {
        *wordArrayIter = (char*)wordItem->what;
        ++wordArrayIter;
    }

    struct CS_Socket *bound = CS_socketBind( 8888, false, false );

    if( bound == NULL ) {
        CS_LOG_ERROR("Could not bind a socket.");
    }

    struct CS_Thread *acceptThread = CS_socketAutoAccept( "cheapProvider", bound, 2048, 32768, true, true, providerThread );

    struct CS_Socket *boundSSL = CS_socketBind( 8899,false,true);

    struct CS_Thread *acceptSSLThread = CS_socketAutoAccept( "cheapSSLProvider", boundSSL, 2048, 32768, true, true, providerThread );

    while( CS_threadIsRunning( acceptThread) &&
           !CS_socketIsClosed( bound ) &&
           CS_threadIsRunning( acceptSSLThread ) &&
           !CS_socketIsClosed( boundSSL ) ) {
        sleep(1);
    }

    return 0;
}


