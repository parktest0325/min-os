#include "frame_buffer.hpp"
#include "logger.hpp"

Error FrameBuffer::Initialize(const FrameBufferConfig& config) {
  config_ = config;

  const auto bits_per_pixel = BitsPerPixel(config_.pixel_format);
  if (bits_per_pixel <= 0) {
    return MAKE_ERROR(Error::kUnknownPixelFormat);
  }

  if (config_.frame_buffer) {
    buffer_.resize(0);
  } else {
    buffer_.resize(
      ((bits_per_pixel + 7) / 8)
      * config_.horizontal_resolution * config_.vertical_resolution);
    config_.frame_buffer = buffer_.data();
    config_.pixels_per_scan_line = config_.horizontal_resolution;
  }

  switch (config_.pixel_format) {
  case kPixelRGBResv8BitPerColor:
    writer_ = std::make_unique<RGBResv8BitPerColorPixelWriter>(config_);
    break;
  case kPixelBGRResv8BitPerColor:
    writer_ = std::make_unique<BGRResv8BitPerColorPixelWriter>(config_);
    break;
  default:
    return MAKE_ERROR(Error::kUnknownPixelFormat);
  }

  return MAKE_ERROR(Error::kSuccess);
}

Error FrameBuffer::Copy(Vector2D<int> pos, const FrameBuffer& src) {
  if (config_.pixel_format != src.config_.pixel_format) {
    return MAKE_ERROR(Error::kUnknownPixelFormat);
  }

  const int bits_per_pixel = BitsPerPixel(config_.pixel_format);
  if (bits_per_pixel <= 0) {
    return MAKE_ERROR(Error::kUnknownPixelFormat);
  }

  const auto dst_w = config_.horizontal_resolution;
  const auto dst_h = config_.vertical_resolution;
  const auto src_w = src.config_.horizontal_resolution;
  const auto src_h = src.config_.vertical_resolution;

  const int dst_x = std::max(pos.x, 0);
  const int dst_y = std::max(pos.y, 0);
  const int src_x = std::max(-pos.x, 0);
  const int src_y = std::max(-pos.y, 0);

  const int bytes_per_pixel = (bits_per_pixel + 7) / 8;

  const int bytes_per_copy_line =
      bytes_per_pixel * std::min(static_cast<int>(src_w - src_x), static_cast<int>(dst_w - dst_x));
  const int copy_line = std::min(static_cast<int>(src_h - src_y), static_cast<int>(dst_h - dst_y));
  if (bytes_per_copy_line <= 0 || copy_line <= 0) {
    // 겹치는 그리기 영역이 없음
    return MAKE_ERROR(Error::kSuccess);
  }

  uint8_t* dst_buf = config_.frame_buffer
      + bytes_per_pixel * (config_.pixels_per_scan_line * dst_y + dst_x);
  const uint8_t* src_buf = src.config_.frame_buffer
      + bytes_per_pixel * (src.config_.pixels_per_scan_line * src_y + src_x);

  for (int dy = 0; dy < copy_line; ++dy) {
    memcpy(dst_buf, src_buf, bytes_per_copy_line);
    dst_buf += bytes_per_pixel * config_.pixels_per_scan_line;
    src_buf += bytes_per_pixel * src.config_.pixels_per_scan_line;
  }

  return MAKE_ERROR(Error::kSuccess);
}

int BitsPerPixel(PixelFormat format) {
  switch (format) {
    case kPixelRGBResv8BitPerColor: return 32;
    case kPixelBGRResv8BitPerColor: return 32;
  }
  return -1;
}