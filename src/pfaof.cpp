#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "rocksdb/utilities/object_registry.h"
#include "rocksdb/file_system.h"
#include "rocksdb/io_status.h"
#include "logging/logging.h"
#include "pf_aof.h"
#include "pf_utils.h"

using namespace ROCKSDB_NAMESPACE;

namespace ROCKSDB_NAMESPACE {
static Logger* mylog = nullptr;

class PfDir : public FSDirectory {
 public:
  std::string dir_name;
  PfDir(std::string _name) : dir_name(_name) {}
  // Fsync directory. Can be called concurrently from multiple threads.
  virtual IOStatus Fsync(const IOOptions& options, IODebugContext* dbg) {
    (void)options;
    (void)dbg;
    return IOStatus::OK();
  }
};

class PfFileLock: public FileLock {
 public:
  PfFileLock() {}
  virtual ~PfFileLock(){};


};
#define SPLIT_FNAME(x)                                      \
    std::string t_name, f_name; do {                        \
    std::string& f=(x);                                     \
    char* p = strrch(f.c_str(), '/');                       \
    t_name = f.substr(0, p - f.c_str());        \
    f_name = f.substr(p - f.c_str() + 1);       \
    }while(0);

class PfAofSeqFile : public rocksdb::FSSequentialFile {
  off_t offset;
  PfAof* aof;
  std::string file_name;
 public:
  PfAofSeqFile(PfAof* _f, const std::string &fname) : offset(0), aof(_f), file_name(fname)
  {}
  ~PfAofSeqFile() {
    delete aof;
  }
  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // After call, result->size() < n only if end of file has been
  // reached (or non-OK status). Read might fail if called again after
  // first result->size() < n.
  //
  // REQUIRES: External synchronization
  virtual IOStatus Read(size_t n, const IOOptions& options, Slice* result,
                        char* scratch, IODebugContext* dbg) {
    IOStatus s;
    (void)options;
    (void)dbg;
    ROCKS_LOG_DEBUG(mylog, "PfAofSeqFile preading %s\n", file_name.c_str());
    ssize_t bytes_read = aof->read(static_cast<void*>(scratch), n, offset);
    offset += bytes_read;
    if (bytes_read < 0) {
      // An error: return a non-ok status
      s = IOStatus::IOError(format_string("Want %ld bytes at offset:%ld in file:%s", n, offset, file_name.c_str()) );
    }
    *result = Slice(scratch, (bytes_read < 0) ? 0 : bytes_read);
    return s;
  }

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual IOStatus Skip(uint64_t n) {
    offset += n;
    return IOStatus::OK();
  }
};

class PfAofRandomFile : virtual public FSRandomAccessFile {
  off_t offset;
  PfAof* aof;
  std::string file_name;
  mutable std::mutex read_mutex;
 public:
  PfAofRandomFile(PfAof* _f, const std::string& fname)
      : offset(0), aof(_f), file_name(fname)
  {}
  ~PfAofRandomFile() { delete aof; }

  // Read up to "n" bytes from the file starting at "offset".
  // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
  // to the data that was read (including if fewer than "n" bytes were
  // successfully read).  May set "*result" to point at data in
  // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
  // "*result" is used.  If an error was encountered, returns a non-OK
  // status.
  //
  // After call, result->size() < n only if end of file has been
  // reached (or non-OK status). Read might fail if called again after
  // first result->size() < n.
  //
  // Safe for concurrent use by multiple threads.
  // If Direct I/O enabled, offset, n, and scratch should be aligned properly.
  IOStatus Read(uint64_t offset_p, size_t n, const IOOptions& options,
                        Slice* result, char* scratch, IODebugContext* dbg)const  override {
    IOStatus s;
    (void)options;
    (void)dbg;
    ROCKS_LOG_DEBUG(mylog, "PfAofSeqFile preading %s\n", file_name.c_str());
    std::lock_guard<std::mutex> guard(read_mutex);
    ssize_t bytes_read = aof->read(static_cast<void*>(scratch), n, offset_p);
    *result = Slice(scratch, (bytes_read < 0) ? 0 : bytes_read);
    if (bytes_read < 0) {
      // An error: return a non-ok status
      s = IOStatus::IOError();
    }
    return s;
  }
};

class PfAofWriteableFile :  public FSWritableFile {
    off_t offset;
    PfAof* aof;
    std::string file_name;
    mutable std::mutex _mutex;

   public:
    PfAofWriteableFile(PfAof* _f, const std::string& fname)
        : offset(0), aof(_f), file_name(fname)
    {}
    ~PfAofWriteableFile() { delete aof; }

    // Append data to the end of the file
    // Note: A WriteableFile object must support either Append or
    // PositionedAppend, so the users cannot mix the two.
    IOStatus Append(const Slice& data, const IOOptions& options,
                            IODebugContext* dbg) override
    {
      (void)options;
      (void)dbg;
      std::lock_guard<std::mutex> guard(_mutex);
      ssize_t rc = aof->append(data.data(), data.size());
      if (rc != (ssize_t)data.size()) return IOStatus::IOError();
      return IOStatus::OK();

    }
    virtual IOStatus Append(const Slice& data, const IOOptions& options,
                            const DataVerificationInfo& /* verification_info */,
                            IODebugContext* dbg) {
      return Append(data, options, dbg);
    }
    IOStatus Close(const IOOptions& options, IODebugContext* dbg)
    {
      (void)options;
      (void)dbg;
      delete aof;
        aof = NULL;
        offset = 0;
        return IOStatus::OK();
    }
    IOStatus Flush(const IOOptions& options, IODebugContext* dbg)
    {
      (void)options;
      (void)dbg;
      aof->sync();
        return IOStatus::OK();
    }

    virtual IOStatus Sync(const IOOptions& options,
                          IODebugContext* dbg)  // sync data
    {
      (void)options;
      (void)dbg;
      aof->sync();
        return IOStatus::OK();
    }
    /*
    * Get the size of valid data in the file.
    */
    virtual uint64_t GetFileSize(const IOOptions& /*options*/,
                                 IODebugContext* /*dbg*/) {
      return aof->file_length();
    }
};

class PfAofFileSystem : public FileSystem {
public:
    PfAofFileSystem()  {}

    // Create a brand new sequentially-readable file with the specified name.
    // On success, stores a pointer to the new file in *result and returns OK.
    // On failure stores nullptr in *result and returns non-OK.  If the file
    // does not exist, returns a non-OK status.
    //
    // The returned file will only be accessed by one thread at a time.
    virtual IOStatus NewSequentialFile(
        const std::string& fname, const FileOptions& file_opts,
        std::unique_ptr<FSSequentialFile>* result, IODebugContext* dbg) {
      (void)file_opts;
      (void)dbg;
      PfAof* aof = pf_open_aof(fname.c_str(), NULL, O_CREAT | O_RDWR,
                               "/etc/pureflash/pf.conf", S5_LIB_VER);
      if (aof == NULL) return IOStatus::PathNotFound();
      *result = std::unique_ptr<FSSequentialFile> (new PfAofSeqFile(aof, fname));
      return IOStatus::OK();
    }

    // Create a brand new random access read-only file with the
    // specified name.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.  If the file does not exist, returns a non-OK
    // status.
    //
    // The returned file may be concurrently accessed by multiple threads.
    virtual IOStatus NewRandomAccessFile(
        const std::string& fname, const FileOptions& file_opts,
        std::unique_ptr<FSRandomAccessFile>* result,
        IODebugContext* dbg)  {
      (void)file_opts;
      (void)dbg;
      PfAof* aof = pf_open_aof(fname.c_str(), NULL, O_CREAT | O_RDWR,
                               "/etc/pureflash/pf.conf", S5_LIB_VER);
      if (aof == NULL) return IOStatus::PathNotFound();
      *result =
          std::unique_ptr<FSRandomAccessFile> (new PfAofRandomFile(aof, fname));
      return IOStatus::OK();
    }

      // Create an object that writes to a new file with the specified
    // name.  Deletes any existing file with the same name and creates a
    // new file.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.
    //
    // The returned file will only be accessed by one thread at a time.
    virtual IOStatus NewWritableFile(const std::string& fname,
                                     const FileOptions& file_opts,
                                     std::unique_ptr<FSWritableFile>* result,
                                     IODebugContext* dbg) {
      (void)file_opts;
      (void)dbg;
      PfAof* aof = pf_open_aof(fname.c_str(), NULL, O_CREAT | O_RDWR,
                               "/etc/pureflash/pf.conf", S5_LIB_VER);
      if (aof == NULL) return IOStatus::PathNotFound();
      *result =
          std::unique_ptr<FSWritableFile> (new PfAofWriteableFile(aof, fname));
      return IOStatus::OK();
    }

    
  // Create an object that represents a directory. Will fail if directory
    // doesn't exist. If the directory exists, it will open the directory
    // and create a new Directory object.
    //
    // On success, stores a pointer to the new Directory in
    // *result and returns OK. On failure stores nullptr in *result and
    // returns non-OK.
    virtual IOStatus NewDirectory(const std::string& dname,
                                  const IOOptions& opt,
                                  std::unique_ptr<FSDirectory>* dir,
                                  IODebugContext* dbg) {
      IOStatus s = CreateDirIfMissing(dname, opt, dbg);
      if (s.code() != Status::kOk) return s;
      *dir = std::unique_ptr<PfDir>(new PfDir(dname));
      return s;
    }

    // Returns OK if the named file exists.
    //         NotFound if the named file does not exist,
    //                  the calling process does not have permission to
    //                  determine whether this file exists, or if the path is
    //                  invalid.
    //         IOError if an IO Error was encountered
    virtual IOStatus FileExists(const std::string& fname,
                                const IOOptions& , IODebugContext* ) {
      int rc = pf_aof_access(fname.c_str(), "/etc/pureflash/pf.conf");
      return rc == 0 ? IOStatus::OK() : IOStatus::NotFound();
    }

    // Store in *result the names of the children of the specified directory.
    // The names are relative to "dir".
    // Original contents of *results are dropped.
    // Returns OK if "dir" exists and "*result" contains its children.
    //         NotFound if "dir" does not exist, the calling process does not
    //         have
    //                  permission to access "dir", or if "dir" is invalid.
    //         IOError if an IO Error was encountered
    virtual IOStatus GetChildren(const std::string& dir_name,
                                 const IOOptions& ,
                                 std::vector<std::string>* result,
                                 IODebugContext* ) {
      std::string t_name = dir_name.back() == '/'
                               ? dir_name.substr(0, dir_name.size() - 1)
                               : dir_name;
        
      int rc = pf_ls_aof_children(t_name.c_str(), "/etc/pureflash/pf.conf", result);
      if(rc == 0) {
        int len = dir_name.size();
        for (std::vector<std::string>::iterator i = result->begin();
             i != result->end(); ++i) {
            *i = i->substr(len + 1);
          fprintf(stderr, "child file:%s\n", i->c_str());
          }
        return IOStatus::OK();
      }
      return 
          (rc == -ENOENT ? IOStatus::PathNotFound()
                                      : IOStatus::IOError(format_string(
                                            "ls_children error, rc:%d", rc)));
    }

    // Delete the named file.
    virtual IOStatus DeleteFile(const std::string& fname,
                                const IOOptions& , IODebugContext* ) {
      int rc = pf_delete_volume(fname.c_str(), "/etc/pureflash/pf.conf");
      return rc == 0? IOStatus::OK(): IOStatus::IOError(
          "DeleteFile error");
    }

    // Truncate the named file to the specified size.
    virtual IOStatus Truncate(const std::string& /*fname*/, size_t /*size*/,
                              const IOOptions& /*options*/,
                              IODebugContext* /*dbg*/) {
      return IOStatus::NotSupported(
          "Truncate is not supported for this PfAofFileSystem");
    }

    // Create the specified directory. Returns error if directory exists.
    virtual IOStatus CreateDir(const std::string& dname ,
                               const IOOptions& , IODebugContext* ) {
      (void)dname;
      return IOStatus::NotSupported(
          "CreateDir is not supported for this PfAofFileSystem");
    }
    // Creates directory if missing. Return Ok if it exists, or successful in
    // Creating.
    virtual IOStatus CreateDirIfMissing(const std::string& dname,
                                        const IOOptions& ,
                                        IODebugContext* ) {
      int rc = 0;
      std::string::size_type s = 0, e = std::string::npos;
      //if (dname[0] == '/') s = 1;
      if (dname.back() == '/') e = dname.size()-1;
      if (s != 0 || e != std::string::npos){
        auto dir_name = dname.substr(s, e);
        rc = pf_create_tenant(dir_name.c_str(), NULL);
      } else {
        rc = pf_create_tenant(dname.c_str(), NULL);
      }
      if (rc == 0)
        return IOStatus::OK();
      else
        return IOStatus::IOError(format_string("Failed create dir, rc:%d", rc));
     
    }

    // Delete the specified directory.
    virtual IOStatus DeleteDir(const std::string& ,
                               const IOOptions& , IODebugContext* ) {
      return IOStatus::NotSupported(
          "DeleteDir is not supported for this PfAofFileSystem");
    }

    // Store the size of fname in *file_size.
    virtual IOStatus GetFileSize(const std::string& fname,
                                 const IOOptions& /*options*/, uint64_t* file_size,
                                 IODebugContext* /*dbg*/) {

        PfAof* f = pf_open_aof(fname.c_str(), NULL, O_RDONLY,"/etc/pureflash/pf.conf", S5_LIB_VER);
      if (f == NULL) return IOStatus::NotFound();
       *file_size = f->file_length();
      delete f;
      return IOStatus::OK();
    }

    // Store the last modification time of fname in *file_mtime.
    virtual IOStatus GetFileModificationTime(const std::string& /*fname*/,
                                             const IOOptions& /*options*/,
                                             uint64_t* /*file_mtime*/,
                                             IODebugContext* /*dbg*/) {
      return IOStatus::NotSupported(
          "GetFileModificationTime is not supported for this PfAofFileSystem");
    }
    // Rename file src to target.
    virtual IOStatus RenameFile(const std::string& src,
                                const std::string& target,
                                const IOOptions& /*options*/, IODebugContext* /*dbg*/) {
      int rc = pf_rename_volume(src.c_str(), target.c_str(), "/etc/pureflash/pf.conf");
      if (rc == 0)
        return IOStatus::OK();
      else
        return IOStatus::IOError(
            format_string("rename volume error, rc:%d", rc));
    }
    // Lock the specified file.  Used to prevent concurrent access to
    // the same db by multiple processes.  On failure, stores nullptr in
    // *lock and returns non-OK.
    //
    // On success, stores a pointer to the object that represents the
    // acquired lock in *lock and returns OK.  The caller should call
    // UnlockFile(*lock) to release the lock.  If the process exits,
    // the lock will be automatically released.
    //
    // If somebody else already holds the lock, finishes immediately
    // with a failure.  I.e., this call does not wait for existing locks
    // to go away.
    //
    // May create the named file if it does not already exist.
    virtual IOStatus LockFile(const std::string& fname,
                              const IOOptions& /*options*/, FileLock** lock,
                              IODebugContext* /*dbg*/) {
      (void)fname;
      fprintf(stderr, "WARN: PfAofFileSystem::LockFile not well implemented\n");
      *lock = new PfFileLock();
      return IOStatus::OK();
    }

    // Release the lock acquired by a previous successful call to LockFile.
    // REQUIRES: lock was returned by a successful LockFile() call
    // REQUIRES: lock has not already been unlocked.
    virtual IOStatus UnlockFile(FileLock* lock, const IOOptions& /*options*/,
                                IODebugContext* /*dbg*/) {
      fprintf(stderr, "WARN: PfAofFileSystem::UnlockFile not well implemented\n");
      delete lock;
      return IOStatus::OK();
    }

  // *path is set to a temporary directory that can be used for testing. It
    // may or many not have just been created. The directory may or may not
    // differ between runs of the same process, but subsequent calls will return
    // the same directory.
    virtual IOStatus GetTestDirectory(const IOOptions& /*options*/,
                                      std::string* /*path*/, IODebugContext* /*dbg*/) {
      return IOStatus::NotSupported(
          "GetTestDirectory is not supported for this PfAofFileSystem");
    }

    // Create and returns a default logger (an instance of EnvLogger) for
    // storing informational messages. Derived classes can override to provide
    // custom logger.
    virtual IOStatus NewLogger(const std::string& /*fname*/,
                               const IOOptions& /*io_opts*/,
                               std::shared_ptr<Logger>* /*result*/,
                               IODebugContext* /*dbg*/) {
      return IOStatus::NotSupported(
          "NewLogger is not supported for this PfAofFileSystem");
    }

    // Get full directory name for this db.
    virtual IOStatus GetAbsolutePath(const std::string& /*db_path*/,
                                     const IOOptions& /*options*/,
                                     std::string* /*output_path*/,
                                     IODebugContext* /*dbg*/) {
      return IOStatus::NotSupported(
          "GetAbsolutePath is not supported for this PfAofFileSystem");
    }

    virtual IOStatus IsDirectory(const std::string& /*path*/,
                                 const IOOptions& /*options*/, bool* /*is_dir*/,
                                 IODebugContext* /*dgb*/) {
      return IOStatus::NotSupported(
          "IsDirectory is not supported for this PfAofFileSystem");
    }
    const char* Name() const override { return "pfaof";   }
};



extern "C" FactoryFunc<FileSystem> pfaof_reg;
FactoryFunc<FileSystem> pfaof_reg =
    ObjectLibrary::Default()->Register<FileSystem>(
        "pfaof", [](const std::string& /* uri */,
                    std::unique_ptr<FileSystem>* f, std::string* /* errmsg */) {
          *f = std::unique_ptr<FileSystem> (new PfAofFileSystem());
          return f->get();
        });



//std::unique_ptr<FileSystem> NewPfAofFileSystem() {
//  return std::unique_ptr<FileSystem>(new PfAofFileSystem());
//}

}  // end namespace ROCKSDB_NAMESPACE
void __PfAof_init() { printf("Just a mock\n"); }