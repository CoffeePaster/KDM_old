#include <kdm.h>
#include <nt/consts.h>
#include <nt/ntos.h>
#include <nt/ntstatus.h>
#include <winternl.h>

PVOID kdm_get_heap(void) {
  return NtCurrentPeb()->ProcessHeap;
}

PVOID kdm_alloc_heap(SIZE_T size) {
  return RtlAllocateHeap(kdm_get_heap(), HEAP_ZERO_MEMORY, size);
}

void kdm_free_heap(PVOID base_address) {
  RtlFreeHeap(kdm_get_heap(), 0, base_address);
}

wchar_t kdm_wclower(wchar_t c) {
	if (c >= 'A' && c <= 'Z')
		return c + 0x20;
	else
		return c;
}

int kdm_wstrcmpi(const wchar_t *s1, const wchar_t *s2) {
	if (s1 == s2) return 0;
	if (s1 == 0)  return -1;
	if (s2 == 0)  return 1;

	wchar_t c1, c2;
	do {
		c1 = kdm_wclower(*s1);
		c2 = kdm_wclower(*s2);
		s1++;
		s2++;
	} while (c1 != 0 && c1 == c2);
	
	return (int)(c1 - c2);
}

wchar_t* kdm_wstrncpy(wchar_t *dst, size_t sz_dst, const wchar_t *src, size_t sz_src)
{
	if (!dst || !src || !sz_dst)
		return dst;

	sz_dst--;
	wchar_t* p = dst;

	while (*src && sz_dst && sz_src) {
		*p = *src;
		p++;
		src++;
		sz_dst--;
		sz_src--;
	}

	*p = 0;
	return dst;
}

wchar_t *kdm_wstrend(const wchar_t *s)
{
	if (!s)
		return 0;

	while (*s)
		s++;

	return (wchar_t*)s;
}

size_t kdm_get_file_size(FILE* file) {
  size_t size;
  if (fseek(file, 0, SEEK_END))
    return -1;

  size = ftell(file);
  if (size == -1L || fseek(file, 0, SEEK_SET))
    return -1;

  return size;
}

bool kdm_write_to_file(const char* filename, char* buffer, size_t size) {
  FILE* file = fopen(filename, "w");
  if (!file)
    return false;

  if (fwrite(buffer, 1, size, file) != size) {
    fclose(file);
    remove(filename);
    return false;
  }

  fclose(file);
  return true;
}

bool kdm_reg_delete_key_recursive(HKEY root, LPCWSTR subkey) {
  WCHAR key_name[MAX_PATH * 2];
  kdm_wstrncpy(key_name, MAX_PATH * 2, subkey, MAX_PATH);
  kdm_reg_delete_key_recursive_intrnl(root, key_name);
}

bool kdm_reg_delete_key_recursive_intrnl(HKEY root, LPCWSTR subkey) {
  // Attempt to delete key as is
  if (!RegDeleteKey(root, subkey))
      return true;

  // Try to open key to check if it exist
  HKEY key;
  LONG res = RegOpenKeyEx(root, subkey, 0, KEY_READ, &key);

  if (res) {
    if (res == ERROR_FILE_NOT_FOUND)
      return true;
    else
      return false;
  }

  // Add slash to the key path if not present
  LPWSTR end = kdm_wstrend(subkey);
  if (*(end - 1) != L'\\') {
    *end = L'\\';
    end++;
    *end = L'\0';
  }

  // Enumerate subkeys and call this func for each
  WCHAR name[MAX_PATH + 1];
  DWORD size = MAX_PATH;
  FILETIME ftWrite;

  if (!RegEnumKeyEx(key, 0, name, &size, NULL, NULL, NULL, &ftWrite)) {
    do {
      kdm_wstrncpy(end, MAX_PATH, name, MAX_PATH);
      if (!kdm_reg_delete_key_recursive_intrnl(root, subkey))
          break;

      size = MAX_PATH;
      res = RegEnumKeyEx(key, 0, name, &size, NULL, NULL, NULL, &ftWrite);
    } while (!res);
  }

  end--;
  *end = L'\0';

  RegCloseKey(key);

  // Delete current key, all it subkeys should be already removed.
  if (!RegDeleteKey(root, subkey))
    return true;

  return true;
}

bool kdm_system_object_exist(LPCWSTR root_directory, LPCWSTR object_name) {
  UNICODE_STRING root_us;
  RtlZeroMemory(&root_us, sizeof(root_us));
  RtlInitUnicodeString(&root_us, root_directory);

  OBJECT_ATTRIBUTES root_attr;
  InitializeObjectAttributes(&root_attr, &root_us, OBJ_CASE_INSENSITIVE, NULL, NULL);

  HANDLE dir_handle;
  if (!NT_SUCCESS(NtOpenDirectoryObject(&dir_handle, DIRECTORY_QUERY, &root_attr)))
    return false;
  
  ULONG ctx = 0;
  while (true) {
    ULONG retlen = 0;
    if (NtQueryDirectoryObject(dir_handle, NULL, 0, TRUE, FALSE, &ctx, &retlen) != STATUS_BUFFER_TOO_SMALL)
      break;

    POBJECT_DIRECTORY_INFORMATION odi = (POBJECT_DIRECTORY_INFORMATION)kdm_alloc_heap(retlen);
    if (!odi)
      break;

    if (!NT_SUCCESS(NtQueryDirectoryObject(dir_handle, odi, retlen, TRUE, FALSE, &ctx, &retlen))) {
      kdm_free_heap(odi);
      break;
    }

    if (kdm_wstrcmpi(odi->Name.Buffer, object_name) == 0) {
      kdm_free_heap(odi);
      NtClose(dir_handle);
      return true;
    }
    kdm_free_heap(odi);
  }

  NtClose(dir_handle);
  return false;
}

bool kdm_query_hvci(bool* enabled, bool* strict_mode, bool* ium_enabled) {
  SYSTEM_CODEINTEGRITY_INFORMATION ci;
  ci.Length = sizeof(ci);

  ULONG retlen;
  if (NT_SUCCESS(NtQuerySystemInformation(SystemCodeIntegrityInformation, &ci, sizeof(ci), &retlen))) {
    bool hvci = (ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_ENABLED) && (ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED);

    if (enabled)
      *enabled = hvci;

    if (strict_mode)
      *strict_mode = hvci && (ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_HVCI_KMCI_STRICTMODE_ENABLED);

    if (ium_enabled)
      *ium_enabled = ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_HVCI_IUM_ENABLED;

    return true;
  }

  return false;
}

bool kdm_detect_hypervisor(char** vendor_string) {
  SYSTEM_HYPERVISOR_DETAIL_INFORMATION hdi;
  RtlZeroMemory(&hdi, sizeof(hdi));

  ULONG retlen;
  if (NT_SUCCESS(NtQuerySystemInformation(SystemHypervisorDetailInformation, &hdi, sizeof(hdi), &retlen))) {
    if (vendor_string)
      *vendor_string = ((PHV_VENDOR_AND_MAX_FUNCTION)&hdi.HvVendorAndMaxFunction.Data)->VendorName;

    return true;
  }
  else {
    int cpu_info[4] = { -1, -1, -1, -1 };
    __cpuid(cpu_info, 1);

    if ((cpu_info[2] >> 31) & 1) {
      __cpuid(cpu_info, 0x40000000);

      if (vendor_string)
        *vendor_string = cpu_info + 1;

      return true;
    }
  }

  return false;
}

FIRMWARE_TYPE kdm_get_firmware_type(void) {
  SYSTEM_BOOT_ENVIRONMENT_INFORMATION sbei;
  RtlZeroMemory(&sbei, sizeof(sbei));

  ULONG retlen;
  if (NT_SUCCESS(NtQuerySystemInformation(SystemBootEnvironmentInformation, &sbei, sizeof(sbei), &retlen)))
    return sbei.FirmwareType;
  else
    return FirmwareTypeUnknown;
}

const char* kdm_get_firmware_type_str(FIRMWARE_TYPE type) {
  switch (type) {
  case FirmwareTypeBios:
    return "BIOS";
  case FirmwareTypeUefi:
    return "UEFI";
  default:
    return "Unknown";
  }
}

char* kdm_read_target_file(const char* filename) {
  FILE* file = fopen(filename, "r");
  if (!file)
    return NULL;

  size_t size = kdm_get_file_size(file);
  if (size == -1) {
    fclose(file);
    return NULL;
  }

  char* buffer = malloc(size);
  if (!buffer) {
    fclose(file);
    return NULL;
  }

  if (fread(buffer, 1, size, file) != size) {
    free(buffer);
    fclose(file);
    return NULL;
  }

  fclose(file);
  return buffer;
}

void kdm_free_target(char* buffer) {
  free(buffer);
}

bool kdm_create_driver_entry(LPCWSTR driver_path, LPCWSTR key_name) {
  UNICODE_STRING driver_image_path;
  RtlInitEmptyUnicodeString(&driver_image_path, NULL, 0);

  if (driver_path && !RtlDosPathNameToNtPathName_U(driver_path, &driver_image_path, NULL, NULL))
      return false;

  HKEY key_handle = NULL;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, key_name, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &key_handle, NULL)) {
    RtlFreeUnicodeString(&driver_image_path);
    return false;
  }

  DWORD data = SERVICE_ERROR_NORMAL;
  if (!RegSetValueEx(key_handle, TEXT("ErrorControl"), 0, REG_DWORD, (BYTE*)&data, sizeof(data))) {
    data = SERVICE_KERNEL_DRIVER;

    if (!RegSetValueEx(key_handle, TEXT("Type"), 0, REG_DWORD, (BYTE*)&data, sizeof(data))) {
      data = SERVICE_DEMAND_START;

      if (!RegSetValueEx(key_handle, TEXT("Start"), 0, REG_DWORD, (BYTE*)&data, sizeof(data))) {
        if (driver_path) {
          if (!RegSetValueEx(key_handle, TEXT("ImagePath"), 0, REG_EXPAND_SZ, (BYTE*)driver_image_path.Buffer, (DWORD)driver_image_path.Length + sizeof(UNICODE_NULL))) {
            RegCloseKey(key_handle);
            RtlFreeUnicodeString(&driver_image_path);
            return true;
          }
        }
        else {
          RegCloseKey(key_handle);
          return true;
        }
      }
    }
  }

  RegCloseKey(key_handle);

  if (driver_path)
    RtlFreeUnicodeString(&driver_image_path);

  return false;
}

bool kdm_load_driver(LPCWSTR driver_name, LPCWSTR driver_path, bool unload_prev_driver) {
  WCHAR buffer[MAX_PATH + 1];
  RtlZeroMemory(buffer, sizeof(buffer));

  if (FAILED(StringCchPrintf(buffer, MAX_PATH, DRIVER_REGKEY, NT_REG_PREP, driver_name)))
    return false;
  
  if (!kdm_create_driver_entry(driver_path, &buffer[RTL_NUMBER_OF(NT_REG_PREP)]))
    return false;
  
  UNICODE_STRING driver_service_name;
  RtlInitUnicodeString(&driver_service_name, buffer);
  
  NTSTATUS status = NtLoadDriver(&driver_service_name);
  if (status == STATUS_IMAGE_ALREADY_LOADED ||
      status == STATUS_OBJECT_NAME_COLLISION ||
      status == STATUS_OBJECT_NAME_EXISTS)
  {
      if (unload_prev_driver
       && NT_SUCCESS(NtUnloadDriver(&driver_service_name))
       && NT_SUCCESS(NtLoadDriver(&driver_service_name)))
       return true;
  }
  else if (status == STATUS_OBJECT_NAME_EXISTS)
    return true;

  return false;
}

bool kdm_unload_driver(LPCWSTR driver_name, bool remove) {
  WCHAR buffer[MAX_PATH + 1];
  RtlZeroMemory(buffer, sizeof(buffer));

  if (FAILED(StringCchPrintf(buffer, MAX_PATH, DRIVER_REGKEY, NT_REG_PREP, driver_name)))
    return false;
  
  if (!kdm_create_driver_entry(NULL, &buffer[RTL_NUMBER_OF(NT_REG_PREP)]))
    return false;
  
  UNICODE_STRING driver_service_name;
  RtlInitUnicodeString(&driver_service_name, buffer);
  
  if (NT_SUCCESS(NtUnloadDriver(&driver_service_name))) {
    if (remove)
      kdm_reg_delete_key_recursive(HKEY_LOCAL_MACHINE, &buffer[RTL_NUMBER_OF(NT_REG_PREP)]);

    return true;
  }

  return false;
}

bool kdm_open_driver(LPCWSTR driver_name, ACCESS_MASK desired_access, PHANDLE device_handle) {
  WCHAR device_path[MAX_PATH + 1];
  RtlZeroMemory(device_path, sizeof(device_path));

  if (FAILED(StringCchPrintf(device_path, MAX_PATH, L"\\DosDevices\\%wS", driver_name)))
    return false;

  UNICODE_STRING device_path_us;
  OBJECT_ATTRIBUTES device_path_oa;

  HANDLE device;
  IO_STATUS_BLOCK iosb;

  RtlInitUnicodeString(&device_path_us, device_path);
  InitializeObjectAttributes(&device_path_oa, &device_path_us, OBJ_CASE_INSENSITIVE, NULL, NULL);

  NTSTATUS status = NtCreateFile(&device, desired_access, &device_path_oa, &iosb, NULL, 0, 0, FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

  if (status == STATUS_OBJECT_NAME_NOT_FOUND
   || status == STATUS_NO_SUCH_DEVICE)
  {
    RtlZeroMemory(device_path, sizeof(device_path));

    if (FAILED(StringCchPrintf(device_path, MAX_PATH, "\\Device\\%wS", driver_name)))
      return false;
      
    RtlInitUnicodeString(&device_path_us, device_path);
    InitializeObjectAttributes(&device_path_oa, &device_path_us, OBJ_CASE_INSENSITIVE, NULL, NULL);

    status = NtCreateFile(&device, desired_access, &device_path_oa, &iosb, NULL, 0, 0, FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
  }

  if (NT_SUCCESS(status)) {
    if (device_handle)
      *device_handle = device;

    return true;
  }

  return false;
}

bool kdm_create_system_admin_access_sd(PSECURITY_DESCRIPTOR* security_descriptor, PACL* default_acl) {
  PSECURITY_DESCRIPTOR sd = (PSECURITY_DESCRIPTOR)kdm_alloc_heap(sizeof(SECURITY_DESCRIPTOR));
  if (!sd)
    return false;

  ULONG acl_size = 0;
  acl_size += RtlLengthRequiredSid(1); //LocalSystem sid
  acl_size += RtlLengthRequiredSid(2); //Admin group sid
  acl_size += sizeof(ACL);
  acl_size += 2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(ULONG));

  PACL acl = (PACL)kdm_alloc_heap(acl_size);
  if (!acl) {
    kdm_free_heap(sd);
    return false;
  }

  if (!NT_SUCCESS(RtlCreateAcl(acl, acl_size, ACL_REVISION))) {
    kdm_free_heap(sd);
    kdm_free_heap(acl);
    return false;
  }

  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;

  UCHAR sid_buffer[2 * sizeof(SID)];
  RtlZeroMemory(sid_buffer, sizeof(sid_buffer));

  // Local System - Generic All
  RtlInitializeSid(sid_buffer, &nt_authority, 1);
  *(RtlSubAuthoritySid(sid_buffer, 0)) = SECURITY_LOCAL_SYSTEM_RID;
  
  RtlAddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, (PSID)sid_buffer);

  // Admins - Generic All
  RtlInitializeSid(sid_buffer, &nt_authority, 2);
  *(RtlSubAuthoritySid(sid_buffer, 0)) = SECURITY_BUILTIN_DOMAIN_RID;
  *(RtlSubAuthoritySid(sid_buffer, 1)) = DOMAIN_ALIAS_RID_ADMINS;
  
  RtlAddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, (PSID)sid_buffer);

  if (!NT_SUCCESS(RtlCreateSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION1))
   || !NT_SUCCESS(RtlSetDaclSecurityDescriptor(sd, TRUE, acl, FALSE)))
  {
    kdm_free_heap(sd);
    kdm_free_heap(acl);
    return false;
  }

  *security_descriptor = sd;
  *default_acl = acl;

  return true;
}