#include "Common.h"
#include <filesystem>
#include <iostream>

int FindDemoMediaFileAbsPath(
  const wchar_t *filePathSuffix,
  std::wstring &absPath
) {
  
  int ret = -1;

  try {

    auto currPath = std::filesystem::current_path();
    std::filesystem::path findPath;
    std::size_t currLen = currPath.native().length();

    do {

      findPath = currPath;
      findPath.append(filePathSuffix);
      if(std::filesystem::exists(findPath) && !std::filesystem::is_directory(findPath)) {
        ret = 0;
        break;
      }
      currPath = currPath.parent_path();
      if(currLen <= currPath.native().length())
        break;
      currLen = currPath.native().length();
    } while(1);

    if(ret == 0)
      absPath = findPath.wstring();

  } catch(std::exception &e) {
    std::cout << "Find path error: " << e.what() << std::endl;
    ret = -1;
  }

  return ret;
}

int FindDemoMediaFileAbsPath(
  const wchar_t *filePathSuffix,
  size_t filePathLen,
  wchar_t *absPath
) {
  std::wstring absPath2;
  int ret = FindDemoMediaFileAbsPath(filePathSuffix, absPath2);
  if(!ret && absPath2.length() + 1 <= filePathLen) {
    std::wmemcpy(absPath, absPath2.c_str(), absPath2.length());
    absPath[absPath2.length()] = 0;
  }

  return ret;
}

