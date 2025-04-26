#include "window.hpp" 

Window::Window(int width, int height) : width_{width}, height_{height} {
  data_.resize(height);
  for (int y = 0; y < height; ++y) {
    data_[y].resize(width);
  }
}

void Window::DrawTo(PixelWriter& writer, Vector2D<int> position) {
  if (!transparent_color_) {
    for (int y = 0; y < Height(); ++y) {
      for (int x = 0; x < Width(); ++x) {
        writer.Write(position + Vector2D<int>{x, y}, At(Vector2D<int>{x, y}));
      }
    }
    return;
  }

  const auto tc = transparent_color_.value();
  for (int y = 0; y < Height(); ++y) {
    for (int x = 0; x < Width(); ++x) {
      const auto c = At(Vector2D<int>{x, y});
      if (c != tc) {
        writer.Write(position + Vector2D<int>{x, y}, c);
      }
    }
  }
  return;
}

void Window::SetTransparentColor(std::optional<PixelColor> c) {
  transparent_color_ = c;
}

Window::WindowWriter* Window::Writer() {
  return &writer_;
}

PixelColor& Window::At(Vector2D<int> pos) {
  return data_[pos.y][pos.x];
}

const PixelColor& Window::At(Vector2D<int> pos) const {
  return data_[pos.y][pos.x];
}

int Window::Width() const {
  return width_;
}

int Window::Height() const {
  return height_;
}