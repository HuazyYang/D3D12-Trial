#pragma once
#include <cstdlib>
#include <string>

extern
int FindDemoMediaFileAbsPath(
  const wchar_t *filePathSuffix,
  size_t filePathLen,
  wchar_t *absPath
);

extern
int FindDemoMediaFileAbsPath(
  const wchar_t *filePathSuffix,
  std::wstring &absPath
);
