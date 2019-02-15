// tapdiag.cpp : Hacky utility to interact with OpenVPN TAP driver to toggle some states for testing.
//

#include <Windows.h>
#include <stdio.h>
#include "tap-windows.h"

HANDLE OpenTapDevice(const char* dev_node);



void PrintHelp(const char* programname)
{
	printf("%s Usage:\n", programname);
	printf("  /link:[on|off] Change link state for TAP adapter\n");


}

int main(int argc, const char** argv)
{
    // Parse parameters
	bool set_link = false;
	bool link_on = false;


	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if (0 == strncmp(arg, "/link:", 6))
		{
			const char* part = arg + 6;
			if (0 == strcmp(part, "on"))
			{
				set_link = true;
				link_on = true;
			}
			else if (0 == strcmp(part, "off"))
			{
				set_link = true;
				link_on = false;
			}
			else
			{
				printf("Unrecognized option: %s\n", arg);
				PrintHelp(argv[0]);
				return 1;
			}
		}
		else
		{
			printf("Unrecognized option: %s\n", arg);
			PrintHelp(argv[0]);
			return 1;
		}
	}

	// Open device
	HANDLE tap_device = OpenTapDevice(NULL);
	if (tap_device == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open TAP device.\n");
		return 1;
	}
	printf("TAP Device opened.\n");

	// Perform action
	if (set_link)
	{
		DWORD mediaStatus = link_on ? 1 : 0;
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


HANDLE OpenTapDevice(const char* dev_node)
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
		char actual_buffer[256];

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
				snprintf(device_path, sizeof(device_path), "%s%s%s",
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