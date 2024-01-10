#ifndef FSM_DARWIN_GCC_H
#define FSM_DARWIN_GCC_H

#ifndef __clang__
/*
 * It is possible to #include CoreFoundation/CoreFoundation.h when compiling
 * with clang, but not with GCC as of time of writing.
 *
 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93082 for details.
 */
typedef unsigned int FSEventStreamCreateFlags;
#define kFSEventStreamEventFlagNone               0x00000000
#define kFSEventStreamEventFlagMustScanSubDirs    0x00000001
#define kFSEventStreamEventFlagUserDropped        0x00000002
#define kFSEventStreamEventFlagKernelDropped      0x00000004
#define kFSEventStreamEventFlagEventIdsWrapped    0x00000008
#define kFSEventStreamEventFlagHistoryDone        0x00000010
#define kFSEventStreamEventFlagRootChanged        0x00000020
#define kFSEventStreamEventFlagMount              0x00000040
#define kFSEventStreamEventFlagUnmount            0x00000080
#define kFSEventStreamEventFlagItemCreated        0x00000100
#define kFSEventStreamEventFlagItemRemoved        0x00000200
#define kFSEventStreamEventFlagItemInodeMetaMod   0x00000400
#define kFSEventStreamEventFlagItemRenamed        0x00000800
#define kFSEventStreamEventFlagItemModified       0x00001000
#define kFSEventStreamEventFlagItemFinderInfoMod  0x00002000
#define kFSEventStreamEventFlagItemChangeOwner    0x00004000
#define kFSEventStreamEventFlagItemXattrMod       0x00008000
#define kFSEventStreamEventFlagItemIsFile         0x00010000
#define kFSEventStreamEventFlagItemIsDir          0x00020000
#define kFSEventStreamEventFlagItemIsSymlink      0x00040000
#define kFSEventStreamEventFlagOwnEvent           0x00080000
#define kFSEventStreamEventFlagItemIsHardlink     0x00100000
#define kFSEventStreamEventFlagItemIsLastHardlink 0x00200000
#define kFSEventStreamEventFlagItemCloned         0x00400000

typedef struct __FSEventStream *FSEventStreamRef;
typedef const FSEventStreamRef ConstFSEventStreamRef;

typedef unsigned int CFStringEncoding;
#define kCFStringEncodingUTF8 0x08000100

typedef const struct __CFString *CFStringRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFRunLoop *CFRunLoopRef;

struct FSEventStreamContext {
    long long version;
    void *cb_data, *retain, *release, *copy_description;
};

typedef struct FSEventStreamContext FSEventStreamContext;
typedef unsigned int FSEventStreamEventFlags;
#define kFSEventStreamCreateFlagNoDefer 0x02
#define kFSEventStreamCreateFlagWatchRoot 0x04
#define kFSEventStreamCreateFlagFileEvents 0x10

typedef unsigned long long FSEventStreamEventId;
#define kFSEventStreamEventIdSinceNow 0xFFFFFFFFFFFFFFFFULL

typedef void (*FSEventStreamCallback)(ConstFSEventStreamRef streamRef,
				      void *context,
				      __SIZE_TYPE__ num_of_events,
				      void *event_paths,
				      const FSEventStreamEventFlags event_flags[],
				      const FSEventStreamEventId event_ids[]);
typedef double CFTimeInterval;
FSEventStreamRef FSEventStreamCreate(void *allocator,
				     FSEventStreamCallback callback,
				     FSEventStreamContext *context,
				     CFArrayRef paths_to_watch,
				     FSEventStreamEventId since_when,
				     CFTimeInterval latency,
				     FSEventStreamCreateFlags flags);
CFStringRef CFStringCreateWithCString(void *allocator, const char *string,
				      CFStringEncoding encoding);
CFArrayRef CFArrayCreate(void *allocator, const void **items, long long count,
			 void *callbacks);
void CFRunLoopRun(void);
void CFRunLoopStop(CFRunLoopRef run_loop);
CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFStringRef kCFRunLoopDefaultMode;
void FSEventStreamSetDispatchQueue(FSEventStreamRef stream, dispatch_queue_t q);
unsigned char FSEventStreamStart(FSEventStreamRef stream);
void FSEventStreamStop(FSEventStreamRef stream);
void FSEventStreamInvalidate(FSEventStreamRef stream);
void FSEventStreamRelease(FSEventStreamRef stream);

#endif /* !clang */
#endif /* FSM_DARWIN_GCC_H */
