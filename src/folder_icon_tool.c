#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#ifndef BIF_NONEWFOLDERBUTTON
#define BIF_NONEWFOLDERBUTTON 0x00000200
#endif
#ifndef RT_MANIFEST
#define RT_MANIFEST 24
#endif

#define PATH_CAP 32768
#define MAX_ICON_IMAGES 256
#define MAX_INI_SIZE (4 * 1024 * 1024)
#define WINDOW_CLASS L"FolderIconToolNativeWindow"
#define APP_TITLE L"文件夹图标助手"

#define ID_FOLDER 101
#define ID_EXE 102
#define ID_APPLY 103
#define ID_BACKUP 104
#define ID_LOG 105

typedef struct {
    BYTE *data;
    DWORD size;
    WORD width;
    WORD height;
} RawImage;

typedef struct {
    BYTE *ico;
    DWORD icoSize;
    RawImage *images;
    int count;
    int maxWidth;
    int maxHeight;
} ExtractedIcon;

typedef struct {
    HWND hwnd;
    HWND title;
    HWND folderGroup;
    HWND folderEdit;
    HWND folderButton;
    HWND exeGroup;
    HWND exeEdit;
    HWND exeButton;
    HWND previewGroup;
    HWND preview;
    HWND previewInfo;
    HWND backupCheck;
    HWND logCheck;
    HWND applyButton;
    HFONT font;
    HFONT titleFont;
    HICON previewIcon;
    ExtractedIcon icon;
    wchar_t folder[PATH_CAP];
    wchar_t exe[PATH_CAP];
    wchar_t logPath[PATH_CAP];
    BOOL logEnabled;
} AppState;

typedef enum {
    ENC_UTF8,
    ENC_UTF8_BOM,
    ENC_UTF16LE,
    ENC_ANSI
} TextEncoding;

static AppState g_app;
static UINT g_dpi = 96;
static HBRUSH g_background;

static int S(int value)
{
    return MulDiv(value, (int)g_dpi, 96);
}

static void set_error(wchar_t *error, size_t capacity, const wchar_t *message)
{
    if (!error || capacity == 0)
        return;
    wcsncpy(error, message, capacity - 1);
    error[capacity - 1] = 0;
}

static BOOL file_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL directory_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL join_path(wchar_t *out, size_t capacity, const wchar_t *directory, const wchar_t *name)
{
    int length = swprintf(out, capacity, L"%s\\%s", directory, name);
    return length > 0 && (size_t)length < capacity;
}

static BOOL read_all(const wchar_t *path, BYTE **data, DWORD *size)
{
    HANDLE file;
    DWORD high = 0;
    DWORD actual = 0;
    BYTE *buffer;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    *size = GetFileSize(file, &high);
    if (*size == INVALID_FILE_SIZE || high != 0 || *size > MAX_INI_SIZE) {
        CloseHandle(file);
        return FALSE;
    }
    buffer = (BYTE *)calloc((size_t)*size + 2, 1);
    if (!buffer) {
        CloseHandle(file);
        return FALSE;
    }
    if (*size && (!ReadFile(file, buffer, *size, &actual, NULL) || actual != *size)) {
        free(buffer);
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    *data = buffer;
    *size = actual;
    return TRUE;
}

static BOOL write_all_retry(const wchar_t *path, const BYTE *data, DWORD size)
{
    int attempt;
    for (attempt = 0; attempt < 20; attempt++) {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            BOOL ok = TRUE;
            while (written < size) {
                DWORD part = 0;
                if (!WriteFile(file, data + written, size - written, &part, NULL) || part == 0) {
                    ok = FALSE;
                    break;
                }
                written += part;
            }
            CloseHandle(file);
            if (ok)
                return TRUE;
        }
        if (attempt + 1 < 20)
            Sleep(100);
    }
    return FALSE;
}

static void make_writable(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return;
    if (attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        SetFileAttributesW(path, attributes & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
}

static WORD read_u16(const BYTE *data, size_t offset)
{
    return (WORD)(data[offset] | ((WORD)data[offset + 1] << 8));
}

static DWORD read_u32(const BYTE *data, size_t offset)
{
    return (DWORD)data[offset] | ((DWORD)data[offset + 1] << 8) |
           ((DWORD)data[offset + 2] << 16) | ((DWORD)data[offset + 3] << 24);
}

static void write_u16(BYTE *data, size_t offset, WORD value)
{
    data[offset] = (BYTE)(value & 0xFF);
    data[offset + 1] = (BYTE)(value >> 8);
}

static void write_u32(BYTE *data, size_t offset, DWORD value)
{
    data[offset] = (BYTE)(value & 0xFF);
    data[offset + 1] = (BYTE)((value >> 8) & 0xFF);
    data[offset + 2] = (BYTE)((value >> 16) & 0xFF);
    data[offset + 3] = (BYTE)((value >> 24) & 0xFF);
}

static void free_icon(ExtractedIcon *icon)
{
    int i;
    if (!icon)
        return;
    if (icon->images) {
        for (i = 0; i < icon->count; i++)
            free(icon->images[i].data);
    }
    free(icon->images);
    free(icon->ico);
    ZeroMemory(icon, sizeof(*icon));
}

typedef struct {
    HMODULE module;
    LPCWSTR groupName;
} EnumContext;

static BOOL CALLBACK find_first_icon(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR context)
{
    EnumContext *state = (EnumContext *)context;
    (void)module;
    (void)type;
    state->groupName = name;
    return FALSE;
}

static BYTE *read_resource(HMODULE module, LPCWSTR name, LPCWSTR type, DWORD *size)
{
    HRSRC resource = FindResourceW(module, name, type);
    HGLOBAL loaded;
    void *pointer;
    BYTE *result;
    if (!resource)
        return NULL;
    *size = SizeofResource(module, resource);
    if (!*size)
        return NULL;
    loaded = LoadResource(module, resource);
    if (!loaded)
        return NULL;
    pointer = LockResource(loaded);
    if (!pointer)
        return NULL;
    result = (BYTE *)malloc(*size);
    if (!result)
        return NULL;
    CopyMemory(result, pointer, *size);
    return result;
}

static BOOL extract_icon_file(const wchar_t *path, ExtractedIcon *out, wchar_t *error, size_t errorCap)
{
    HMODULE module;
    EnumContext context;
    BYTE *group = NULL;
    DWORD groupSize = 0;
    int count;
    int i;
    DWORD total;
    BYTE *ico;
    DWORD offset;
    ExtractedIcon result;

    ZeroMemory(out, sizeof(*out));
    if (!file_exists(path)) {
        set_error(error, errorCap, L"所选 EXE 文件不存在。");
        return FALSE;
    }
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        set_error(error, errorCap, L"无法读取该程序的图标资源。");
        return FALSE;
    }

    context.module = module;
    context.groupName = NULL;
    EnumResourceNamesW(module, MAKEINTRESOURCEW(RT_GROUP_ICON), find_first_icon, (LONG_PTR)&context);
    if (!context.groupName) {
        FreeLibrary(module);
        set_error(error, errorCap, L"该 EXE 没有可提取的图标资源。");
        return FALSE;
    }

    group = read_resource(module, context.groupName, MAKEINTRESOURCEW(RT_GROUP_ICON), &groupSize);
    if (!group || groupSize < 6) {
        free(group);
        FreeLibrary(module);
        set_error(error, errorCap, L"图标资源目录不完整。");
        return FALSE;
    }
    count = read_u16(group, 4);
    if (read_u16(group, 2) != 1 || count <= 0 || count > MAX_ICON_IMAGES ||
        groupSize < (DWORD)(6 + count * 14)) {
        free(group);
        FreeLibrary(module);
        set_error(error, errorCap, L"图标资源目录格式异常。");
        return FALSE;
    }

    ZeroMemory(&result, sizeof(result));
    result.images = (RawImage *)calloc((size_t)count, sizeof(RawImage));
    if (!result.images) {
        free(group);
        FreeLibrary(module);
        set_error(error, errorCap, L"内存不足。");
        return FALSE;
    }

    total = (DWORD)(6 + count * 16);
    for (i = 0; i < count; i++) {
        size_t entry = (size_t)(6 + i * 14);
        DWORD imageSize = read_u32(group, entry + 8);
        WORD resourceId = read_u16(group, entry + 12);
        BYTE *image;
        if (imageSize == 0 || imageSize > MAX_INI_SIZE) {
            free_icon(&result);
            free(group);
            FreeLibrary(module);
            set_error(error, errorCap, L"图标图像数据异常。");
            return FALSE;
        }
        image = read_resource(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(RT_ICON), &result.images[i].size);
        if (!image || result.images[i].size != imageSize) {
            free(image);
            free_icon(&result);
            free(group);
            FreeLibrary(module);
            set_error(error, errorCap, L"无法读取图标图像。");
            return FALSE;
        }
        result.images[i].data = image;
        result.images[i].width = group[entry] ? group[entry] : 256;
        result.images[i].height = group[entry + 1] ? group[entry + 1] : 256;
        if (result.images[i].width > result.maxWidth)
            result.maxWidth = result.images[i].width;
        if (result.images[i].height > result.maxHeight)
            result.maxHeight = result.images[i].height;
        total += imageSize;
    }
    result.count = count;

    ico = (BYTE *)calloc(total, 1);
    if (!ico) {
        free_icon(&result);
        free(group);
        FreeLibrary(module);
        set_error(error, errorCap, L"内存不足。");
        return FALSE;
    }
    write_u16(ico, 0, 0);
    write_u16(ico, 2, 1);
    write_u16(ico, 4, (WORD)count);
    offset = (DWORD)(6 + count * 16);
    for (i = 0; i < count; i++) {
        size_t entry = (size_t)(6 + i * 14);
        size_t target = (size_t)(6 + i * 16);
        CopyMemory(ico + target, group + entry, 8);
        write_u32(ico, target + 8, result.images[i].size);
        write_u32(ico, target + 12, offset);
        CopyMemory(ico + offset, result.images[i].data, result.images[i].size);
        offset += result.images[i].size;
    }
    result.ico = ico;
    result.icoSize = total;
    free(group);
    FreeLibrary(module);
    *out = result;
    return TRUE;
}

static wchar_t *decode_bytes(const BYTE *data, DWORD size, UINT codepage, BOOL strict)
{
    int wideLength;
    wchar_t *text;
    DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    wideLength = MultiByteToWideChar(codepage, flags, (LPCSTR)data, (int)size, NULL, 0);
    if (wideLength <= 0)
        return NULL;
    text = (wchar_t *)calloc((size_t)wideLength + 1, sizeof(wchar_t));
    if (!text)
        return NULL;
    MultiByteToWideChar(codepage, flags, (LPCSTR)data, (int)size, text, wideLength);
    text[wideLength] = 0;
    return text;
}

static wchar_t *read_text_file(const wchar_t *path, TextEncoding *encoding)
{
    BYTE *data = NULL;
    DWORD size = 0;
    wchar_t *text = NULL;

    *encoding = ENC_UTF8;
    if (!file_exists(path)) {
        text = (wchar_t *)calloc(1, sizeof(wchar_t));
        return text;
    }
    if (!read_all(path, &data, &size))
        return NULL;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        *encoding = ENC_UTF8_BOM;
        text = decode_bytes(data + 3, size - 3, CP_UTF8, TRUE);
    } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        size_t characters = (size - 2) / 2;
        size_t i;
        *encoding = ENC_UTF16LE;
        text = (wchar_t *)calloc(characters + 1, sizeof(wchar_t));
        if (text) {
            for (i = 0; i < characters; i++)
                text[i] = (wchar_t)read_u16(data, 2 + i * 2);
        }
    } else if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        size_t characters = (size - 2) / 2;
        size_t i;
        *encoding = ENC_UTF8;
        text = (wchar_t *)calloc(characters + 1, sizeof(wchar_t));
        if (text) {
            for (i = 0; i < characters; i++) {
                WORD value = (WORD)((data[2 + i * 2] << 8) | data[3 + i * 2]);
                text[i] = (wchar_t)value;
            }
        }
    } else if (size == 0) {
        text = (wchar_t *)calloc(1, sizeof(wchar_t));
    } else {
        text = decode_bytes(data, size, CP_UTF8, TRUE);
        if (text) {
            *encoding = ENC_UTF8;
        } else {
            text = decode_bytes(data, size, CP_ACP, FALSE);
            *encoding = ENC_ANSI;
        }
    }
    free(data);
    return text;
}

static BOOL write_text_file(const wchar_t *path, const wchar_t *text, TextEncoding encoding)
{
    BYTE *payload = NULL;
    DWORD payloadSize = 0;
    BOOL ok;

    if (encoding == ENC_UTF16LE) {
        size_t length = wcslen(text);
        size_t i;
        payloadSize = (DWORD)(2 + length * 2);
        payload = (BYTE *)malloc(payloadSize);
        if (!payload)
            return FALSE;
        payload[0] = 0xFF;
        payload[1] = 0xFE;
        for (i = 0; i < length; i++) {
            payload[2 + i * 2] = (BYTE)(text[i] & 0xFF);
            payload[3 + i * 2] = (BYTE)((text[i] >> 8) & 0xFF);
        }
    } else {
        UINT codepage = encoding == ENC_ANSI ? CP_ACP : CP_UTF8;
        int bytes = WideCharToMultiByte(codepage, 0, text, -1, NULL, 0, NULL, NULL);
        int bom = encoding == ENC_UTF8_BOM ? 3 : 0;
        int wideLength = (int)wcslen(text);
        (void)wideLength;
        if (bytes <= 0)
            return FALSE;
        payloadSize = (DWORD)(bytes - 1 + bom);
        payload = (BYTE *)malloc(payloadSize ? payloadSize : 1);
        if (!payload)
            return FALSE;
        if (bom) {
            payload[0] = 0xEF;
            payload[1] = 0xBB;
            payload[2] = 0xBF;
        }
        WideCharToMultiByte(codepage, 0, text, -1, (LPSTR)(payload + bom), bytes, NULL, NULL);
    }
    ok = write_all_retry(path, payload, payloadSize);
    free(payload);
    return ok;
}

typedef struct {
    wchar_t *data;
    size_t length;
    size_t capacity;
} WString;

static BOOL ws_init(WString *text, size_t capacity)
{
    text->data = (wchar_t *)calloc(capacity, sizeof(wchar_t));
    text->length = 0;
    text->capacity = capacity;
    return text->data != NULL;
}

static BOOL ws_append(WString *text, const wchar_t *value)
{
    size_t add = wcslen(value);
    if (text->length + add + 1 > text->capacity) {
        size_t next = (text->capacity + add + 1024) * 2;
        wchar_t *grown = (wchar_t *)realloc(text->data, next * sizeof(wchar_t));
        if (!grown)
            return FALSE;
        text->data = grown;
        text->capacity = next;
    }
    CopyMemory(text->data + text->length, value, add * sizeof(wchar_t));
    text->length += add;
    text->data[text->length] = 0;
    return TRUE;
}

static void ws_line(WString *text, const wchar_t *line)
{
    if (text->length)
        ws_append(text, L"\r\n");
    ws_append(text, line);
}

static BOOL line_key_is(const wchar_t *line, const wchar_t *wanted)
{
    const wchar_t *start = line;
    const wchar_t *end;
    size_t length;
    while (*start && iswspace(*start))
        start++;
    end = start;
    while (*end && *end != L'=' && !iswspace(*end))
        end++;
    length = (size_t)(end - start);
    return length == wcslen(wanted) && _wcsnicmp(start, wanted, length) == 0;
}

static wchar_t *build_desktop_ini(const wchar_t *source)
{
    static const wchar_t setting[] = L"IconResource=.FolderIcon\\icon.ico,0";
    wchar_t *work = _wcsdup(source && *source ? source : L"");
    wchar_t *context = NULL;
    wchar_t *line;
    WString output;
    BOOL inShell = FALSE;
    BOOL foundShell = FALSE;
    BOOL hasSetting = FALSE;

    if (!work || !ws_init(&output, wcslen(source ? source : L"") + 2048)) {
        free(work);
        return NULL;
    }

    line = wcstok_s(work, L"\r\n", &context);
    while (line) {
        wchar_t *trimmed = line;
        size_t length;
        while (*trimmed && iswspace(*trimmed))
            trimmed++;
        length = wcslen(trimmed);
        while (length && iswspace(trimmed[length - 1]))
            trimmed[--length] = 0;

        if (trimmed[0] == L'[' && length > 1 && trimmed[length - 1] == L']') {
            if (inShell && !hasSetting) {
                ws_line(&output, setting);
                hasSetting = TRUE;
            }
            inShell = _wcsicmp(trimmed, L"[.ShellClassInfo]") == 0;
            if (inShell) {
                foundShell = TRUE;
                hasSetting = FALSE;
            }
            ws_line(&output, trimmed);
        } else if (inShell && line_key_is(trimmed, L"IconResource")) {
            ws_line(&output, setting);
            hasSetting = TRUE;
        } else {
            ws_line(&output, trimmed);
        }
        line = wcstok_s(NULL, L"\r\n", &context);
    }
    if (inShell && !hasSetting)
        ws_line(&output, setting);
    if (!foundShell) {
        if (output.length)
            ws_line(&output, L"");
        ws_line(&output, L"[.ShellClassInfo]");
        ws_line(&output, setting);
    }
    ws_append(&output, L"\r\n");
    free(work);
    return output.data;
}

static void timestamp_text(wchar_t *buffer, size_t capacity)
{
    SYSTEMTIME time;
    GetLocalTime(&time);
    swprintf(buffer, capacity, L"%04d%02d%02d_%02d%02d%02d_%03d_%lu",
             time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
             time.wMilliseconds, (unsigned long)(GetTickCount() % 1000));
}

static BOOL ensure_directory(const wchar_t *path)
{
    if (directory_exists(path))
        return TRUE;
    if (CreateDirectoryW(path, NULL))
        return TRUE;
    return directory_exists(path);
}

static BOOL is_protected_folder(const wchar_t *full, wchar_t *error, size_t errorCap)
{
    wchar_t volume[PATH_CAP];
    wchar_t windows[PATH_CAP];
    size_t windowsLength;

    if (GetVolumePathNameW(full, volume, PATH_CAP)) {
        size_t length = wcslen(volume);
        while (length > 3 && volume[length - 1] == L'\\')
            volume[--length] = 0;
        if (_wcsicmp(volume, full) == 0) {
            set_error(error, errorCap, L"磁盘根目录受到保护，请选择普通文件夹。");
            return TRUE;
        }
    }
    if (!GetWindowsDirectoryW(windows, PATH_CAP))
        wcscpy(windows, L"C:\\Windows");
    windowsLength = wcslen(windows);
    while (windowsLength > 3 && windows[windowsLength - 1] == L'\\')
        windows[--windowsLength] = 0;
    if (_wcsicmp(full, windows) == 0 ||
        (_wcsnicmp(full, windows, windowsLength) == 0 && full[windowsLength] == L'\\')) {
        set_error(error, errorCap, L"Windows 系统目录受到保护。");
        return TRUE;
    }
    return FALSE;
}

static BOOL apply_icon(const wchar_t *folderInput, const BYTE *icoData, DWORD icoSize,
                       BOOL backupExisting, wchar_t *error, size_t errorCap)
{
    wchar_t full[PATH_CAP];
    DWORD fullLength;
    wchar_t dataFolder[PATH_CAP];
    wchar_t iconPath[PATH_CAP];
    wchar_t desktopPath[PATH_CAP];
    wchar_t backupFolder[PATH_CAP];
    wchar_t backupPath[PATH_CAP];
    wchar_t stamp[80];
    BOOL hadDesktop;
    BOOL hadIcon;
    DWORD oldDesktopAttributes = 0;
    TextEncoding encoding;
    wchar_t *sourceText = NULL;
    wchar_t *newText = NULL;
    DWORD folderAttributes;

    if (!folderInput || !*folderInput || !directory_exists(folderInput)) {
        set_error(error, errorCap, L"所选文件夹不存在。");
        return FALSE;
    }
    fullLength = GetFullPathNameW(folderInput, PATH_CAP, full, NULL);
    if (!fullLength || fullLength >= PATH_CAP) {
        set_error(error, errorCap, L"文件夹路径过长。");
        return FALSE;
    }
    while (fullLength > 3 && full[fullLength - 1] == L'\\')
        full[--fullLength] = 0;
    if (is_protected_folder(full, error, errorCap))
        return FALSE;
    if (!icoData || !icoSize) {
        set_error(error, errorCap, L"没有可应用的图标。");
        return FALSE;
    }
    if (!join_path(dataFolder, PATH_CAP, full, L".FolderIcon") ||
        !join_path(iconPath, PATH_CAP, dataFolder, L"icon.ico") ||
        !join_path(desktopPath, PATH_CAP, full, L"desktop.ini")) {
        set_error(error, errorCap, L"文件夹路径过长。");
        return FALSE;
    }
    if (!ensure_directory(dataFolder)) {
        set_error(error, errorCap, L"无法创建图标数据目录，请检查文件夹写入权限。");
        return FALSE;
    }
    hadDesktop = file_exists(desktopPath);
    hadIcon = file_exists(iconPath);
    timestamp_text(stamp, 80);

    if (backupExisting && (hadDesktop || hadIcon)) {
        if (!join_path(backupFolder, PATH_CAP, dataFolder, L"backup") || !ensure_directory(backupFolder)) {
            set_error(error, errorCap, L"无法创建备份目录。");
            return FALSE;
        }
        if (hadDesktop) {
            swprintf(backupPath, PATH_CAP, L"%s\\desktop_%s.ini", backupFolder, stamp);
            if (!CopyFileW(desktopPath, backupPath, FALSE)) {
                set_error(error, errorCap, L"无法备份原有 desktop.ini。");
                return FALSE;
            }
        }
        if (hadIcon) {
            swprintf(backupPath, PATH_CAP, L"%s\\icon_%s.ico", backupFolder, stamp);
            if (!CopyFileW(iconPath, backupPath, FALSE)) {
                set_error(error, errorCap, L"无法备份原有图标。");
                return FALSE;
            }
        }
    }

    make_writable(iconPath);
    if (!write_all_retry(iconPath, icoData, icoSize)) {
        set_error(error, errorCap, L"无法写入图标文件，请检查文件夹权限。");
        return FALSE;
    }
    SetFileAttributesW(iconPath, FILE_ATTRIBUTE_HIDDEN);

    sourceText = read_text_file(desktopPath, &encoding);
    if (!sourceText) {
        set_error(error, errorCap, L"无法读取原有 desktop.ini。");
        return FALSE;
    }
    newText = build_desktop_ini(sourceText);
    free(sourceText);
    if (!newText) {
        set_error(error, errorCap, L"内存不足。");
        return FALSE;
    }
    if (hadDesktop)
        oldDesktopAttributes = GetFileAttributesW(desktopPath);
    make_writable(desktopPath);
    if (!write_text_file(desktopPath, newText, encoding)) {
        free(newText);
        set_error(error, errorCap, L"无法写入 desktop.ini，请检查文件夹权限。");
        return FALSE;
    }
    free(newText);
    SetFileAttributesW(desktopPath, oldDesktopAttributes | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    SetFileAttributesW(dataFolder, GetFileAttributesW(dataFolder) | FILE_ATTRIBUTE_HIDDEN);
    folderAttributes = GetFileAttributesW(full);
    SetFileAttributesW(full, folderAttributes | FILE_ATTRIBUTE_READONLY);

    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, full, NULL);
    SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, full, NULL);
    return TRUE;
}

static void initialize_log_path(void)
{
    DWORD length = GetModuleFileNameW(NULL, g_app.logPath, PATH_CAP);
    if (length && length < PATH_CAP) {
        wchar_t *slash = wcsrchr(g_app.logPath, L'\\');
        if (slash)
            wcscpy(slash + 1, L"FolderIconTool.log");
    }
}

static void app_log(const wchar_t *message)
{
    HANDLE file;
    SYSTEMTIME time;
    char line[2048];
    char utf8[1536];
    int prefix;
    int converted;

    if (!g_app.logEnabled || !g_app.logPath[0])
        return;
    GetLocalTime(&time);
    prefix = sprintf(line, "%04d-%02d-%02d %02d:%02d:%02d.%03d  ",
                     time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                     time.wSecond, time.wMilliseconds);
    converted = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof(utf8), NULL, NULL);
    if (converted > 0) {
        int copied = converted - 1;
        if (prefix + copied + 2 >= (int)sizeof(line))
            copied = (int)sizeof(line) - prefix - 3;
        CopyMemory(line + prefix, utf8, copied);
        prefix += copied;
    }
    line[prefix++] = '\r';
    line[prefix++] = '\n';

    file = CreateFileW(g_app.logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, line, (DWORD)prefix, &written, NULL);
        CloseHandle(file);
    }
}

static HWND make_control(const wchar_t *className, const wchar_t *text, DWORD style,
                         int x, int y, int width, int height, int id, HFONT font)
{
    HWND hwnd = CreateWindowExW(
        (style & WS_EX_CLIENTEDGE) ? WS_EX_CLIENTEDGE : 0,
        className, text, WS_CHILD | WS_VISIBLE | (style & ~WS_EX_CLIENTEDGE),
        S(x), S(y), S(width), S(height), g_app.hwnd, (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL), NULL);
    if (hwnd && font)
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    return hwnd;
}

static void set_preview_placeholder(void)
{
    if (g_app.preview) {
        SendMessageW(g_app.preview, STM_SETICON, 0, 0);
        SetWindowTextW(g_app.previewInfo, L"选择 EXE 后显示图标。");
    }
    if (g_app.previewIcon) {
        DestroyIcon(g_app.previewIcon);
        g_app.previewIcon = NULL;
    }
}

static void update_apply_button(void)
{
    BOOL ready = g_app.icon.ico != NULL && directory_exists(g_app.folder) && file_exists(g_app.exe);
    EnableWindow(g_app.applyButton, ready);
}

static void show_extracted_preview(void)
{
    int best = 0;
    int i;
    HICON icon;
    wchar_t info[256];

    for (i = 1; i < g_app.icon.count; i++) {
        if ((int)g_app.icon.images[i].width * g_app.icon.images[i].height >
            (int)g_app.icon.images[best].width * g_app.icon.images[best].height)
            best = i;
    }
    icon = CreateIconFromResourceEx(g_app.icon.images[best].data, g_app.icon.images[best].size,
                                    TRUE, 0x00030000, S(128), S(128), 0);
    if (!icon) {
        set_preview_placeholder();
        MessageBoxW(g_app.hwnd, L"Windows 无法显示提取到的图标。", L"预览失败",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (g_app.previewIcon)
        DestroyIcon(g_app.previewIcon);
    g_app.previewIcon = icon;
    SendMessageW(g_app.preview, STM_SETICON, (WPARAM)icon, 0);
    swprintf(info, 256, L"%d 个尺寸  ·  最大 %d × %d",
             g_app.icon.count, g_app.icon.maxWidth, g_app.icon.maxHeight);
    SetWindowTextW(g_app.previewInfo, info);
}

static void choose_folder(void)
{
    BROWSEINFOW browse;
    wchar_t display[260];
    wchar_t path[PATH_CAP];
    PIDLIST_ABSOLUTE id;

    ZeroMemory(&browse, sizeof(browse));
    browse.hwndOwner = g_app.hwnd;
    browse.pszDisplayName = display;
    browse.lpszTitle = L"选择要更换图标的文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    id = SHBrowseForFolderW(&browse);
    if (!id)
        return;
    if (SHGetPathFromIDListW(id, path)) {
        wcscpy(g_app.folder, path);
        SetWindowTextW(g_app.folderEdit, g_app.folder);
        app_log(L"选择文件夹。");
        update_apply_button();
    }
    CoTaskMemFree(id);
}

static void choose_exe(void)
{
    OPENFILENAMEW dialog;
    wchar_t file[PATH_CAP];
    wchar_t error[256];
    ExtractedIcon extracted;

    ZeroMemory(file, sizeof(file));
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_app.hwnd;
    dialog.lpstrFilter = L"可执行程序 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = PATH_CAP;
    dialog.lpstrInitialDir = directory_exists(g_app.folder) ? g_app.folder : NULL;
    dialog.lpstrTitle = L"选择要提取图标的 EXE";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    dialog.nFilterIndex = 1;
    dialog.lpstrDefExt = L"exe";
    if (!GetOpenFileNameW(&dialog))
        return;

    ZeroMemory(&extracted, sizeof(extracted));
    if (!extract_icon_file(file, &extracted, error, 256)) {
        MessageBoxW(g_app.hwnd, error, L"无法提取图标", MB_OK | MB_ICONWARNING);
        return;
    }
    set_preview_placeholder();
    free_icon(&g_app.icon);
    g_app.icon = extracted;
    wcscpy(g_app.exe, file);
    SetWindowTextW(g_app.exeEdit, g_app.exe);
    show_extracted_preview();
    app_log(L"提取并预览图标。");
    update_apply_button();
}

static void apply_selected_icon(void)
{
    wchar_t error[256];

    if (!g_app.icon.ico) {
        MessageBoxW(g_app.hwnd, L"请先选择并预览 EXE 图标。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    EnableWindow(g_app.applyButton, FALSE);
    if (apply_icon(g_app.folder, g_app.icon.ico, g_app.icon.icoSize,
                   SendMessageW(g_app.backupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED,
                   error, 256)) {
        app_log(L"图标应用成功。");
        MessageBoxW(g_app.hwnd, L"文件夹图标已应用。", APP_TITLE, MB_OK | MB_ICONINFORMATION);
    } else {
        app_log(L"图标应用失败。");
        MessageBoxW(g_app.hwnd, error, L"应用失败", MB_OK | MB_ICONERROR);
    }
    update_apply_button();
}

static void toggle_log(void)
{
    g_app.logEnabled = SendMessageW(g_app.logCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (g_app.logEnabled)
        app_log(L"运行日志已启用。");
}

static void create_ui(HWND hwnd)
{
    int normalHeight = -MulDiv(9, (int)g_dpi, 72);
    int titleHeight = -MulDiv(18, (int)g_dpi, 72);

    g_app.hwnd = hwnd;
    g_app.font = CreateFontW(normalHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_app.titleFont = CreateFontW(titleHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_app.title = make_control(L"STATIC", APP_TITLE, SS_LEFT, 24, 16, 620, 42, 0, g_app.titleFont);
    g_app.folderGroup = make_control(L"BUTTON", L"1  选择要修改的文件夹", BS_GROUPBOX,
                                     20, 64, 640, 88, 0, g_app.font);
    g_app.folderEdit = make_control(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP | WS_EX_CLIENTEDGE,
                                    36, 98, 466, 28, 0, g_app.font);
    g_app.folderButton = make_control(L"BUTTON", L"选择文件夹", BS_PUSHBUTTON | WS_TABSTOP,
                                      516, 96, 122, 32, ID_FOLDER, g_app.font);

    g_app.exeGroup = make_control(L"BUTTON", L"2  选择要提取图标的 EXE", BS_GROUPBOX,
                                  20, 158, 640, 88, 0, g_app.font);
    g_app.exeEdit = make_control(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP | WS_EX_CLIENTEDGE,
                                 36, 192, 466, 28, 0, g_app.font);
    g_app.exeButton = make_control(L"BUTTON", L"选择 EXE", BS_PUSHBUTTON | WS_TABSTOP,
                                   516, 190, 122, 32, ID_EXE, g_app.font);

    g_app.previewGroup = make_control(L"BUTTON", L"3  预览所选程序的图标", BS_GROUPBOX,
                                      20, 252, 640, 178, 0, g_app.font);
    g_app.preview = make_control(L"STATIC", L"", SS_ICON | SS_CENTERIMAGE | SS_REALSIZECONTROL,
                                 44, 284, 128, 128, 0, NULL);
    g_app.previewInfo = make_control(L"STATIC", L"选择 EXE 后显示图标。", SS_LEFT,
                                     198, 306, 420, 60, 0, g_app.font);

    g_app.backupCheck = make_control(L"BUTTON", L"备份现有设置", BS_AUTOCHECKBOX | WS_TABSTOP,
                                     28, 446, 160, 28, ID_BACKUP, g_app.font);
    g_app.logCheck = make_control(L"BUTTON", L"运行日志", BS_AUTOCHECKBOX | WS_TABSTOP,
                                  206, 446, 160, 28, ID_LOG, g_app.font);
    g_app.applyButton = make_control(L"BUTTON", L"提取并应用", BS_DEFPUSHBUTTON | WS_TABSTOP,
                                     500, 438, 140, 40, ID_APPLY, g_app.font);
    SendMessageW(g_app.backupCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_app.logCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    update_apply_button();

#ifdef UI_TEST
    {
        wchar_t test[PATH_CAP];
        wchar_t *separator;
        if (GetEnvironmentVariableW(L"FOLDER_ICON_TOOL_UI_TEST", test, PATH_CAP)) {
            separator = wcschr(test, L'|');
            if (separator) {
                wchar_t error[256];
                *separator = 0;
                wcscpy(g_app.folder, test);
                wcscpy(g_app.exe, separator + 1);
                SetWindowTextW(g_app.folderEdit, g_app.folder);
                SetWindowTextW(g_app.exeEdit, g_app.exe);
                if (extract_icon_file(g_app.exe, &g_app.icon, error, 256)) {
                    show_extracted_preview();
                    update_apply_button();
                }
            }
        }
    }
#endif
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        create_ui(hwnd);
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0) {
            switch (LOWORD(wParam)) {
            case ID_FOLDER:
                choose_folder();
                return 0;
            case ID_EXE:
                choose_exe();
                return 0;
            case ID_APPLY:
                apply_selected_icon();
                return 0;
            case ID_LOG:
                toggle_log();
                return 0;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        set_preview_placeholder();
        free_icon(&g_app.icon);
        if (g_app.font)
            DeleteObject(g_app.font);
        if (g_app.titleFont)
            DeleteObject(g_app.titleFont);
        app_log(L"程序退出。");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

#ifndef CONSOLE_TEST
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
#else
static int run_application(HINSTANCE instance, int show)
#endif
{
    WNDCLASSW windowClass;
    RECT rect;
    RECT work;
    int width;
    int height;
    int x;
    int y;
    MSG message;
    INITCOMMONCONTROLSEX controls;
    HDC screen;

#ifndef CONSOLE_TEST
    (void)previous;
    (void)commandLine;
#endif
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    screen = GetDC(NULL);
    g_dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    if (!g_dpi)
        g_dpi = 96;
    g_background = GetSysColorBrush(COLOR_BTNFACE);
    initialize_log_path();

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    windowClass.hbrBackground = g_background;
    windowClass.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassW(&windowClass)) {
        MessageBoxW(NULL, L"无法注册程序窗口。", APP_TITLE, MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    rect.left = 0;
    rect.top = 0;
    rect.right = S(680);
    rect.bottom = S(508);
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                       FALSE, 0);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    x = work.left + ((work.right - work.left) - width) / 2;
    y = work.top + ((work.bottom - work.top) - height) / 2;

    g_app.hwnd = CreateWindowExW(
        0, WINDOW_CLASS, APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, width, height, NULL, NULL, instance, NULL);
    if (!g_app.hwnd) {
        MessageBoxW(NULL, L"无法创建程序窗口。", APP_TITLE, MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    ShowWindow(g_app.hwnd, show);
    UpdateWindow(g_app.hwnd);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return 0;
}

#ifdef CONSOLE_TEST
int main(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 1;
    if (!argv)
        return 1;
    if (argc == 4 && _wcsicmp(argv[1], L"--extract") == 0) {
        ExtractedIcon icon;
        wchar_t error[256];
        if (extract_icon_file(argv[2], &icon, error, 256)) {
            wprintf(L"icons=%d max=%dx%d bytes=%lu\n", icon.count, icon.maxWidth,
                    icon.maxHeight, (unsigned long)icon.icoSize);
            if (write_all_retry(argv[3], icon.ico, icon.icoSize))
                result = 0;
            free_icon(&icon);
        } else {
            fwprintf(stderr, L"%s\n", error);
        }
    } else if (argc == 4 && _wcsicmp(argv[1], L"--apply") == 0) {
        ExtractedIcon icon;
        wchar_t error[256];
        if (extract_icon_file(argv[3], &icon, error, 256)) {
            if (apply_icon(argv[2], icon.ico, icon.icoSize, TRUE, error, 256)) {
                wprintf(L"applied\n");
                result = 0;
            } else {
                fwprintf(stderr, L"%s\n", error);
            }
            free_icon(&icon);
        } else {
            fwprintf(stderr, L"%s\n", error);
        }
    } else if (argc == 3 && _wcsicmp(argv[1], L"--ui-test") == 0) {
        result = run_application(GetModuleHandleW(NULL), SW_SHOW);
    } else {
        wprintf(L"usage: --extract <exe> <ico> | --apply <folder> <exe> | --ui-test <unused>\n");
    }
    LocalFree(argv);
    return result;
}
#endif
