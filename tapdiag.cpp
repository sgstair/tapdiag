// tapdiag.cpp : Hacky utility to interact with OpenVPN TAP driver to toggle some states for testing.
//

#include <Windows.h>
#include <stdio.h>
#include "tap-windows.h"

HANDLE OpenTapDiagDevice(const char* dev_node);



void PrintHelp(const char* programname)
{
	printf("%s Usage:\n", programname);
	printf("  /enable				Add registry key to enable TAP diag device\n");
	printf("  /disable				Set registry key to disable TAP diag device\n");
	printf("  /link:[on|off]		Change link state for TAP adapter\n");
	printf("  /setq:[on|off|always] Configure adding of 802.1Q priority/vlan headers\n");


}

int main(int argc, const char** argv)
{
    // Parse parameters
	bool set_link = false;
	bool link_on = false;
	bool set_enable = false;
	bool enable_value = false;
	bool set_qos = false;
	ULONG qos_value = 0;


	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if (arg[0] == '-' || arg[0] == '/')
		{
			arg++;
			if (0 == strcmp(arg, "enable"))
			{
				set_enable = true;
				enable_value = true;
				continue;
			}
			else if (0 == strcmp(arg, "disable"))
			{
				set_enable = true;
				enable_value = true;
				continue;
			}
			else if (0 == strncmp(arg, "link:", 5))
			{
				const char* part = arg + 5;
				if (0 == strcmp(part, "on"))
				{
					set_link = true;
					link_on = true;
					continue;
				}
				else if (0 == strcmp(part, "off"))
				{
					set_link = true;
					link_on = false;
					continue;
				}
				else
				{
					printf("Unrecognized option: %s\n", arg);
					PrintHelp(argv[0]);
					return 1;
				}
			}
			else if (0 == strncmp(arg, "setq:", 5))
			{
				const char* part = arg + 5;
				if (0 == strcmp(part, "on"))
				{
					set_qos = true;
					qos_value = TAP_PRIORITY_BEHAVIOR_ENABLED;
					continue;
				}
				else if (0 == strcmp(part, "off"))
				{
					set_qos = true;
					qos_value = TAP_PRIORITY_BEHAVIOR_NOPRIORITY;
					continue;
				}
				else if (0 == strcmp(part, "always"))
				{
					set_qos = true;
					qos_value = TAP_PRIORITY_BEHAVIOR_ADDALWAYS;
					continue;
				}
				else
				{
					printf("Unrecognized option: %s\n", arg);
					PrintHelp(argv[0]);
					return 1;
				}
			}

		}
		printf("Unrecognized option: %s\n", arg);
		PrintHelp(argv[0]);
		return 1;
	}


	if (set_enable)
	{
		int exitcode = 1;
		const char* regPath = "SYSTEM\\CurrentControlSet\\Services\\tap0901";
		printf("Setting tapdiag enable flag...\n");
		HKEY driverKey = NULL;

		LONG result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, regPath, 0, KEY_SET_VALUE, &driverKey);
		if (result != ERROR_SUCCESS)
		{
			printf("Failed to open driver registry key. (%d)\n", result);
		}
		else
		{
			DWORD newValue = enable_value ? 1 : 0;

			result = RegSetValueEx(driverKey, "TapDiag", 0, REG_DWORD, (BYTE*)&newValue, sizeof(DWORD));

			if (result == ERROR_SUCCESS)
			{
				printf("Successfully set enable flag.\n");
				printf("HLKM:%s - TapDiag REG_DWORD = %d\n", regPath, newValue);
				printf("\nNote: You must restart the tap driver for this to take effect!\n\n");
				exitcode = 0;
			}
			else
			{
				printf("Failed to set registry value. (%d)\n", result);
			}
			RegCloseKey(driverKey);
		}
		return exitcode;
	}



	// Open device
	HANDLE tap_device = OpenTapDiagDevice(NULL);
	if (tap_device == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open TAP device.\n");
		return 1;
	}
	printf("TAP Device opened.\n");

	// Perform action

	if (set_link)
	{
		ULONG mediaStatus = link_on ? 1 : 0;
		DWORD bytesReturned = 0;

		if (DeviceIoControl(tap_device, TAP_WIN_IOCTL_SET_MEDIA_STATUS,
			&mediaStatus, sizeof(mediaStatus),
			NULL, 0, &bytesReturned, NULL))
		{
			printf("Successfully set media status.\n");
		}
		else
		{
			printf("Failed to set media status.\n");
			return 1;
		}
	}

	if (set_qos)
	{
		DWORD bytesReturned = 0;

		if (DeviceIoControl(tap_device, TAP_WIN_IOCTL_PRIORITY_BEHAVIOR,
			&qos_value, sizeof(qos_value),
			NULL, 0, &bytesReturned, NULL))
		{
			printf("Successfully set 802.1Q configuration.\n");
		}
		else
		{
			printf("Failed to set 802.1Q configuration\n");
			return 1;
		}
	}


	CloseHandle(tap_device);
	return 0;
}



#define TAP_WIN_COMPONENT_ID "tap0901"
struct tap_reg
{
	const char *guid;
	struct tap_reg *next;
};

const char * string_alloc(const char * src_string)
{
	size_t len = strlen(src_string);
	char* new_string = new char[len + 1];
	strncpy_s(new_string, len+1, src_string, len);
	return new_string;
}

const struct tap_reg *
get_tap_reg()
{
	HKEY adapter_key;
	LONG status;
	DWORD len;
	struct tap_reg *first = NULL;
	struct tap_reg *last = NULL;
	int i = 0;

	status = RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,
		ADAPTER_KEY,
		0,
		KEY_READ,
		&adapter_key);

	if (status != ERROR_SUCCESS)
	{
		//msg(M_FATAL, "Error opening registry key: %s", ADAPTER_KEY);
	}

	while (true)
	{
		char enum_name[256];
		char unit_string[256];
		HKEY unit_key;
		char component_id_string[] = "ComponentId";
		char component_id[256];
		char net_cfg_instance_id_string[] = "NetCfgInstanceId";
		char net_cfg_instance_id[256];
		DWORD data_type;

		len = sizeof(enum_name);
		status = RegEnumKeyEx(
			adapter_key,
			i,
			enum_name,
			&len,
			NULL,
			NULL,
			NULL,
			NULL);
		if (status == ERROR_NO_MORE_ITEMS)
		{
			break;
		}
		else if (status != ERROR_SUCCESS)
		{
			//msg(M_FATAL, "Error enumerating registry subkeys of key: %s",
			//	ADAPTER_KEY);
		}

		snprintf(unit_string, sizeof(unit_string), "%s\\%s",
			ADAPTER_KEY, enum_name);

		status = RegOpenKeyEx(
			HKEY_LOCAL_MACHINE,
			unit_string,
			0,
			KEY_READ,
			&unit_key);

		if (status != ERROR_SUCCESS)
		{
			//dmsg(D_REGISTRY, "Error opening registry key: %s", unit_string);
		}
		else
		{
			len = sizeof(component_id);
			status = RegQueryValueEx(
				unit_key,
				component_id_string,
				NULL,
				&data_type,
				(BYTE*)component_id,
				&len);

			if (status != ERROR_SUCCESS || data_type != REG_SZ)
			{
				//dmsg(D_REGISTRY, "Error opening registry key: %s\\%s",
				//	unit_string, component_id_string);
			}
			else
			{
				len = sizeof(net_cfg_instance_id);
				status = RegQueryValueEx(
					unit_key,
					net_cfg_instance_id_string,
					NULL,
					&data_type,
					(BYTE*)net_cfg_instance_id,
					&len);

				if (status == ERROR_SUCCESS && data_type == REG_SZ)
				{
					if (!strcmp(component_id, TAP_WIN_COMPONENT_ID))
					{
						struct tap_reg *reg = new tap_reg;
						reg->guid = string_alloc(net_cfg_instance_id);

						/* link into return list */
						if (!first)
						{
							first = reg;
						}
						if (last)
						{
							last->next = reg;
						}
						last = reg;
					}
				}
			}
			RegCloseKey(unit_key);
		}
		++i;
	}

	RegCloseKey(adapter_key);
	return first;
}


HANDLE OpenTapDiagDevice(const char* dev_node)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	char device_path[256];
	const char *device_guid = NULL;
	/*
	 * Lookup the device name in the registry, using the --dev-node high level name.
	 */
	{
		const struct tap_reg *tap_reg = get_tap_reg();
		//const struct panel_reg *panel_reg = get_panel_reg();
		//char actual_buffer[256];

		if (tap_reg == NULL)
		{
			printf("No TAP devices found.\n");
			return h;
		}

		// Don't allow selecting by dev node for now. Just pick the first device.
#if 0
		if (dev_node)
		{
			/* Get the device GUID for the device specified with --dev-node. */
			device_guid = get_device_guid(dev_node, actual_buffer, sizeof(actual_buffer), tap_reg, panel_reg, &gc);

			if (!device_guid)
			{
				msg(M_FATAL, "TAP-Windows adapter '%s' not found", dev_node);
			}

			/* Open Windows TAP-Windows adapter */
			snprintf(device_path, sizeof(device_path), "%s%s%s",
				USERMODEDEVICEDIR,
				device_guid,
				TAP_WIN_SUFFIX);

			tt->hand = CreateFile(
				device_path,
				GENERIC_READ | GENERIC_WRITE,
				0,                /* was: FILE_SHARE_READ */
				0,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
				0
			);

			if (tt->hand == INVALID_HANDLE_VALUE)
			{
				msg(M_ERR, "CreateFile failed on TAP device: %s", device_path);
			}
		}
		else
#endif
		{
			int device_number = 0;
			const struct tap_reg *tap = tap_reg;
			/* Try opening all TAP devices until we find one available */
			while (tap != NULL)
			{
	/*			device_guid = get_unspecified_device_guid(device_number,
					actual_buffer,
					sizeof(actual_buffer),
					tap_reg,
					panel_reg,
					&gc);*/

				device_guid = tap_reg->guid;

				if (!device_guid)
				{
					//msg(M_FATAL, "All TAP-Windows adapters on this system are currently in use.");
				}

				/* Open Windows TAP-Windows adapter */
				snprintf(device_path, sizeof(device_path), "%s%s%sdiag",
					USERMODEDEVICEDIR,
					device_guid,
					TAP_WIN_SUFFIX);

				printf("Opening device '%s'\n", device_path);

				h = CreateFile(
					device_path,
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					0,
					OPEN_EXISTING,
					FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED,
					0
				);

				

				if (h == INVALID_HANDLE_VALUE)
				{
					//msg(D_TUNTAP_INFO, "CreateFile failed on TAP device: %s", device_path);					
				}
				else
				{
					return h;
				}

				tap = tap->next;
			}
		}

	}
	return h;


	//{
	//	ULONG info[3];
	//	CLEAR(info);
	//	if (DeviceIoControl(tt->hand, TAP_WIN_IOCTL_GET_VERSION,
	//		&info, sizeof(info),
	//		&info, sizeof(info), &len, NULL))
	//	{
	//		msg(D_TUNTAP_INFO, "TAP-Windows Driver Version %d.%d %s",
	//			(int)info[0],
	//			(int)info[1],
	//			(info[2] ? "(DEBUG)" : ""));

	//	}

	//}
}