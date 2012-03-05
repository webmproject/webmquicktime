#include "quicktime_util.h"

#include <QuickTime/QuickTime.h>
#include <inttypes.h>

#include <new>

#include "log.h"
#include "WebMExportVersions.h"

// TODO(tomfinegan): Move this, it doesn't belong here.
#define WEBMQT_DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);                      \
  void operator=(const TypeName&)

namespace {
// Utility class for managing the list of |FourCharCode|s returned from
// |QTGetComponentPropertyInfo| when the available compression formats list
// (|kQTSCAudioPropertyID_AvailableCompressionFormatList|) is requested.
class CompressionFormats {
 public:
  CompressionFormats(const FourCharCode* ptr_formats, int format_count)
      : formats_(ptr_formats),
        num_formats_(format_count) {}
  ~CompressionFormats() {
    delete[] formats_;
  };

  const FourCharCode* formats() const { return formats_; }
  int num_formats() const { return num_formats_; }

 private:
  const FourCharCode* formats_;
  const int num_formats_;
  WEBMQT_DISALLOW_COPY_AND_ASSIGN(CompressionFormats);
};
}  // namespace

// Returns list of compression formats available to QuickTime. Caller owns
// memory allocated via new for |CompressionFormats*|. Returns NULL on failure.
const CompressionFormats* const AudioCompressionFormatsList(
    ComponentInstance component) {
  if (!component) {
    dbg_printf("[webm:%s] NULL component.\n", __func__);
    return NULL;
  }

  // Request number of bytes required to store compression format list.
  ByteCount format_list_size = 0;
  int err = QTGetComponentPropertyInfo(
      component,
      kQTPropertyClass_SCAudio,
      kQTSCAudioPropertyID_AvailableCompressionFormatList,
      NULL,
      &format_list_size,
      NULL);
  if (err != noErr) {
    dbg_printf("[webm:%s] cannot get format list size (%d).\n", __func__, err);
    return NULL;
  }

  // Convert byte count to number of |FourCharCode|s.
  const int num_formats =
      (format_list_size + (sizeof(OSType) - 1)) / sizeof(OSType);

  // Allocate the format list storage.
  FourCharCode* const ptr_format_list =
      new (std::nothrow) FourCharCode[num_formats];

  if (ptr_format_list) {
    // Request the format list from QuickTime.
    err = QTGetComponentProperty(
        component,
        kQTPropertyClass_SCAudio,
        kQTSCAudioPropertyID_AvailableCompressionFormatList,
        format_list_size,
        reinterpret_cast<void*>(ptr_format_list),
        NULL);  // NULL, ignore amount of storage consumed.
    if (err != noErr) {
      dbg_printf("[webm:%s] format list read failed (%d).\n", __func__, err);
      delete[] ptr_format_list;
      return NULL;
    }
  }

  const CompressionFormats* const ptr_formats =
      new (std::nothrow) CompressionFormats(ptr_format_list, num_formats);
  return ptr_formats;
}

// Returns true when the XiphQT Vorbis |FourCharCode| format identifier,
// |kAudioFormatXiphVorbis|, is included in the list of formats returned by
// |AudioCompressionFormatsList()|.
bool CanExportVorbisAudio() {
  bool can_export_vorbis = false;
  ComponentInstance vorbis_component = NULL;
  const OSType compression_type = StandardCompressionType;
  const OSType sub_type = StandardCompressionSubTypeAudio;

  if (!OpenADefaultComponent(compression_type, sub_type, &vorbis_component)) {
    // |ptr_formats| is owned by this function, and must be |delete|d.
    const CompressionFormats* ptr_formats =
        AudioCompressionFormatsList(vorbis_component);
    if (ptr_formats && ptr_formats->formats()) {
      const FourCharCode* const ptr_format_list = ptr_formats->formats();
      for (int i = 0; i < ptr_formats->num_formats(); ++i) {
        if (ptr_format_list[i] == kAudioFormatXiphVorbis) {
          can_export_vorbis = true;
          break;
        }
      }
    }
    delete ptr_formats;

    // Close the component-- just probing capabilities here.
    CloseComponent(vorbis_component);
 }

  dbg_printf("[webm:%s] can_export_vorbis=%s.\n", __func__,
             (can_export_vorbis ? "true" : "false"));
  return can_export_vorbis;
}
