#pragma once

#include <memory>
#include "graphics.hpp"
#include "window.hpp"

class Console {
public:
  static const int kMaxRows = 120, kMaxColumns = 240;

  Console(const PixelColor& fg_color, const PixelColor& bg_color,
          int rows, int columns);
  void PutString(const char* s);
  void SetWriter(PixelWriter* writer);
  void SetWindow(const std::shared_ptr<Window>& window);
  void SetLayerID(unsigned int layer_id);
  unsigned int LayerID() const;
  int Rows() const { return rows_; }
  int Columns() const { return columns_; }

private:
  void Newline();
  void Refresh();

  PixelWriter* writer_;
  std::shared_ptr<Window> window_;
  const PixelColor fg_color_, bg_color_;
  int rows_, columns_;
  char buffer_[kMaxRows][kMaxColumns + 1];
  int cursor_row_, cursor_column_;
  unsigned int layer_id_;
};

extern Console* console;
void InitializeConsole();