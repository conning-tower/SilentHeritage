#include "app/android_documents.h"

#include <SDL3/SDL_system.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/error.h"

namespace sh {

namespace {

// GameActivity's static methods. Looked up once from SDL's main
// thread: FindClass on threads attached from native code does not see the
// app's classes, while the global class reference works on any thread.
struct Bridge {
  jclass activity = nullptr;
  jmethodID pick_folder = nullptr;
  jmethodID tree_root = nullptr;
  jmethodID list_documents = nullptr;
  jmethodID open_document = nullptr;
};

// Returns the JNI environment of the calling thread (attached by SDL).
JNIEnv* jni_env() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) {
    throw std::runtime_error(std::format("no JNI environment: {}", SDL_GetError()));
  }
  return env;
}

Bridge make_bridge() {
  JNIEnv* env = jni_env();
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (activity == nullptr) {
    throw std::runtime_error(std::format("no Android activity: {}", SDL_GetError()));
  }
  const jclass local = env->GetObjectClass(activity);
  env->DeleteLocalRef(activity);
  Bridge b;
  b.activity = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  b.pick_folder = env->GetStaticMethodID(b.activity, "pickFolder", "()Ljava/lang/String;");
  b.tree_root = env->GetStaticMethodID(b.activity, "documentTreeRoot", "(Ljava/lang/String;)Ljava/lang/String;");
  b.list_documents = env->GetStaticMethodID(b.activity, "listDocuments",
                                            "(Ljava/lang/String;Ljava/lang/String;)[Ljava/lang/String;");
  b.open_document = env->GetStaticMethodID(b.activity, "openDocument", "(Ljava/lang/String;Ljava/lang/String;)I");
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    throw std::runtime_error("GameActivity lacks the methods of the asset setup");
  }
  return b;
}

const Bridge& bridge() {
  static const Bridge b = make_bridge();
  return b;
}

// Frees the local references made in its scope (threads attached from
// native code never return to Java, where they would be freed).
class LocalFrame {
 public:
  explicit LocalFrame(JNIEnv* env) : env_(env) { env_->PushLocalFrame(16); }
  ~LocalFrame() { env_->PopLocalFrame(nullptr); }
  LocalFrame(const LocalFrame&) = delete;
  LocalFrame& operator=(const LocalFrame&) = delete;

 private:
  JNIEnv* env_;
};

// Returns whether a Java exception was thrown (logged and cleared).
bool java_failed(JNIEnv* env) {
  if (!env->ExceptionCheck()) {
    return false;
  }
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}

// Returns the content of `s`, empty for null.
std::string to_string(JNIEnv* env, jstring s) {
  if (s == nullptr) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(s, nullptr);
  std::string out(chars);
  env->ReleaseStringUTFChars(s, chars);
  return out;
}

// Reads file descriptor `fd` to its end and closes it; nothing on a read
// error.
std::optional<std::vector<std::uint8_t>> read_fd(int fd) {
  std::vector<std::uint8_t> data;
  struct stat st {};
  if (fstat(fd, &st) == 0 && st.st_size > 0) {
    data.reserve(static_cast<std::size_t>(st.st_size));
  }
  std::uint8_t buffer[1 << 16];
  for (;;) {
    const ssize_t n = ::read(fd, buffer, sizeof buffer);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      ::close(fd);
      return std::nullopt;
    }
    if (n == 0) {
      break;
    }
    data.insert(data.end(), buffer, buffer + n);
  }
  ::close(fd);
  return data;
}

// A document read through its file descriptor, which it owns.
class FdSource final : public ByteSource {
 public:
  FdSource(int fd, std::uint64_t size, std::string name) : fd_(fd), size_(size), name_(std::move(name)) {}
  FdSource(const FdSource&) = delete;
  FdSource& operator=(const FdSource&) = delete;
  ~FdSource() override { ::close(fd_); }

  std::uint64_t size() const override { return size_; }

  void read(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    if (offset > size_ || out.size() > size_ - offset) {
      throw DataError(std::format("{}: {} bytes at {} are past its end", name_, out.size(), offset));
    }
    std::size_t done = 0;
    while (done < out.size()) {
      const ssize_t n = ::pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        throw DataError(std::format("cannot read {}", name_));
      }
      done += static_cast<std::size_t>(n);
    }
  }

  std::string describe() const override { return name_; }

 private:
  int fd_;
  std::uint64_t size_;
  std::string name_;
};

class AndroidDocumentTree final : public Install::DocumentTree {
 public:
  explicit AndroidDocumentTree(std::string uri) : uri_(std::move(uri)) {
    JNIEnv* env = jni_env();
    const LocalFrame frame(env);
    const auto root = static_cast<jstring>(
        env->CallStaticObjectMethod(bridge().activity, bridge().tree_root, env->NewStringUTF(uri_.c_str())));
    if (java_failed(env) || root == nullptr) {
      throw DataError(std::format("{} is not a folder", uri_));
    }
    root_ = to_string(env, root);
  }

  std::string root() const override { return root_; }

  std::vector<Entry> children(const std::string& id) const override {
    JNIEnv* env = jni_env();
    const LocalFrame frame(env);
    const auto list = static_cast<jobjectArray>(env->CallStaticObjectMethod(
        bridge().activity, bridge().list_documents, env->NewStringUTF(uri_.c_str()), env->NewStringUTF(id.c_str())));
    if (java_failed(env) || list == nullptr) {
      throw DataError(std::format("cannot list the folder {} (permission revoked or folder removed?)", uri_));
    }
    const jsize n = env->GetArrayLength(list);
    std::vector<Entry> out;
    for (jsize i = 0; i + 3 < n; i += 4) {
      // The strings of each entry are released before the next ones are
      // made, within the frame's capacity.
      const LocalFrame entry_frame(env);
      Entry e;
      e.name = to_string(env, static_cast<jstring>(env->GetObjectArrayElement(list, i)));
      e.id = to_string(env, static_cast<jstring>(env->GetObjectArrayElement(list, i + 1)));
      e.directory = to_string(env, static_cast<jstring>(env->GetObjectArrayElement(list, i + 2))) == "d";
      const std::string size = to_string(env, static_cast<jstring>(env->GetObjectArrayElement(list, i + 3)));
      std::from_chars(size.data(), size.data() + size.size(), e.size);
      out.push_back(std::move(e));
    }
    return out;
  }

  std::optional<std::vector<std::uint8_t>> read(const std::string& id) const override {
    const int fd = open_fd(id);
    if (fd < 0) {
      return std::nullopt;
    }
    return read_fd(fd);
  }

  std::shared_ptr<const ByteSource> open(const std::string& id, const std::string& name) const override {
    const int fd = open_fd(id);
    struct stat st {};
    if (fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
      return std::make_shared<FdSource>(fd, static_cast<std::uint64_t>(st.st_size),
                                        std::format("{} (in {})", name, uri_));
    }
    if (fd >= 0) {
      ::close(fd);  // a pipe from the provider: read whole
    }
    return Install::DocumentTree::open(id, name);
  }

  std::string describe() const override { return uri_; }

 private:
  // Returns a file descriptor of document `id`, -1 when it cannot be opened.
  int open_fd(const std::string& id) const {
    JNIEnv* env = jni_env();
    const LocalFrame frame(env);
    const jint fd = env->CallStaticIntMethod(bridge().activity, bridge().open_document,
                                             env->NewStringUTF(uri_.c_str()), env->NewStringUTF(id.c_str()));
    return java_failed(env) ? -1 : fd;
  }

  std::string uri_;
  std::string root_;
};

}  // namespace

std::optional<std::string> pick_android_folder() {
  JNIEnv* env = jni_env();
  const LocalFrame frame(env);
  const auto uri = static_cast<jstring>(env->CallStaticObjectMethod(bridge().activity, bridge().pick_folder));
  if (java_failed(env)) {
    throw std::runtime_error("the folder picker failed");
  }
  if (uri == nullptr) {
    return std::nullopt;
  }
  return to_string(env, uri);
}

std::shared_ptr<const Install::DocumentTree> android_document_tree(const std::string& tree_uri) {
  return std::make_shared<AndroidDocumentTree>(tree_uri);
}

}  // namespace sh
