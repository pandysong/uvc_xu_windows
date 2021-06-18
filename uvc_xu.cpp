#include <iostream>
#include <comsvcs.h>
#include <atlstr.h>
#include <vidcap.h>
#include <windows.h>
#include <ksproxy.h>
#include <strmif.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>

BOOL GetVideoCaptureFilter(IBaseFilter** piFilter, USHORT usVid, USHORT usPid);
BOOL GetNodeId(IBaseFilter* pCaptureFilter, int* pNodeId);
BOOL GetIKsControl(IBaseFilter* pCaptureFilter, IKsControl** ppKsControl);
HRESULT FindExtensionNode(IKsTopologyInfo* pIksTopologyInfo, GUID extensionGuid, DWORD* pNodeId);

BOOL UvcXuCommand(USHORT usVid, USHORT usPid, UCHAR controlId, UCHAR *buffer, ULONG *BytesReturned, bool get)
{
	BOOL bSuccess = FALSE;
	IKsControl* pKsControl = NULL;
	IBaseFilter* pCaptureFilter = NULL;
	int nNodeId;
	KSP_NODE ExtensionProp;
	HRESULT hr;
	GUID rk_extension_unit_guid = { 0x41769EA2, 0x04DE, 0xE347, 0x8B, 0x2B, 0xF4, 0x34, 0x1A, 0xFF, 0x00, 0x3B };
	if (!GetVideoCaptureFilter(&pCaptureFilter, usVid, usPid))
	{
		std::cout << "GetVideoCaptureFilter failed\n";
		goto Exit_UvcSwitch;
	}

	if (!GetNodeId(pCaptureFilter, &nNodeId))
	{
		std::cout << "GetNodeId failed\n";
		goto Exit_UvcSwitch;
	}

	if (!GetIKsControl(pCaptureFilter, &pKsControl))
	{
		std::cout << "GetIKsControl failed\n";
		goto Exit_UvcSwitch;
	}

	ExtensionProp.Property.Set = rk_extension_unit_guid;
	ExtensionProp.Property.Id = controlId;
	ExtensionProp.NodeId = nNodeId;

    if (get)
        ExtensionProp.Property.Flags = KSPROPERTY_TYPE_GET |
            KSPROPERTY_TYPE_TOPOLOGY;
    else
        ExtensionProp.Property.Flags = KSPROPERTY_TYPE_SET |
            KSPROPERTY_TYPE_TOPOLOGY;
	hr = pKsControl->KsProperty((PKSPROPERTY)&ExtensionProp, sizeof(ExtensionProp), (PVOID)buffer, 60, BytesReturned);
	if (hr != S_OK)
	{
		std::cout << "KsProperty failed\n";
		goto Exit_UvcSwitch;
	}
	bSuccess = TRUE;

Exit_UvcSwitch:
	if (pKsControl)
	{
		pKsControl->Release();
	}
	if (pCaptureFilter)
	{
		pCaptureFilter->Release();
	}

	return bSuccess;
}
BOOL GetVideoCaptureFilter(IBaseFilter** piFilter, USHORT usVid, USHORT usPid)
{
	HRESULT hr;
	ICreateDevEnum* piCreateDevEnum;
	CString s;
	BOOL bSuccess = FALSE;

	CoInitialize(NULL);

	HRESULT hResult = CoCreateInstance(
		CLSID_SystemDeviceEnum,
		NULL,
		CLSCTX_INPROC_SERVER,
		__uuidof(piCreateDevEnum),
		reinterpret_cast<void**>(&piCreateDevEnum));
	if (SUCCEEDED(hResult))
	{
		IEnumMoniker* piEnumMoniker;
		hResult = piCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &piEnumMoniker, 0);
		piCreateDevEnum->Release();

		if (S_OK == hResult)
			hResult = piEnumMoniker->Reset();

		if (S_OK == hResult)
		{
			// Enumerate KS devices
			ULONG cFetched;
			IMoniker* piMoniker;
			CString strDevicePath;
			CString strMatch;

			while ((hResult = piEnumMoniker->Next(1, &piMoniker, &cFetched)) == S_OK)
			{

				IPropertyBag* pBag = 0;
				hr = piMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pBag);
				if (SUCCEEDED(hr))
				{
					VARIANT var;
					var.vt = VT_BSTR;
					hr = pBag->Read(_T("DevicePath"), &var, NULL); //FriendlyName
					if (hr == NOERROR)
					{
						strDevicePath = var.bstrVal;
						SysFreeString(var.bstrVal);
						strDevicePath.MakeUpper();

						strMatch.Format(_T("VID_%04X&PID_%04X"), usVid, usPid);
						if (strDevicePath.Find(strMatch) != -1)
						{
							hResult = piMoniker->BindToObject(
								NULL,
								NULL,
								IID_IBaseFilter,
								(void**)(piFilter));
							pBag->Release();
							piMoniker->Release();

							if (SUCCEEDED(hResult))
							{
								bSuccess = TRUE;
							}
							break;
						}
					}
					pBag->Release();
				}
				piMoniker->Release();
			}

			piEnumMoniker->Release();
		}
	}

	return bSuccess;
}
BOOL GetNodeId(IBaseFilter* pCaptureFilter, int* pNodeId)
{

	IKsTopologyInfo* pKsToplogyInfo;
	HRESULT hResult;
	DWORD dwNode;

	if (!pCaptureFilter)
		return FALSE;

	hResult = pCaptureFilter->QueryInterface(
		__uuidof(IKsTopologyInfo),
		(void**)(&pKsToplogyInfo));
	if (S_OK == hResult)
	{
		hResult = FindExtensionNode(pKsToplogyInfo, KSNODETYPE_DEV_SPECIFIC, &dwNode); //KSNODETYPE_DEV_SPECIFIC PROPSETID_VIDCAP_EXTENSION_UNIT
		pKsToplogyInfo->Release();

		if (S_OK == hResult)
		{
			*pNodeId = dwNode;
			return TRUE;
		}
	}
	return FALSE;
}
HRESULT FindExtensionNode(IKsTopologyInfo* pIksTopologyInfo, GUID extensionGuid, DWORD* pNodeId)
{
	DWORD numberOfNodes;
	HRESULT hResult = S_FALSE;

	hResult = pIksTopologyInfo->get_NumNodes(&numberOfNodes);
	if (SUCCEEDED(hResult))
	{
		DWORD i;
		GUID nodeGuid;
		for (i = 0; i < numberOfNodes; i++)
		{
			if (SUCCEEDED(pIksTopologyInfo->get_NodeType(i, &nodeGuid)))
			{
				if (IsEqualGUID(extensionGuid, nodeGuid))
				{  // Found the extension node 
					*pNodeId = i;
					hResult = S_OK;
					break;
				}
			}
		}

		if (i == numberOfNodes)
		{ // Did not find the node 
			hResult = S_FALSE;
		}
	}
	return hResult;
}
BOOL GetIKsControl(IBaseFilter* pCaptureFilter, IKsControl** ppKsControl)
{
	if (!pCaptureFilter)
		return FALSE;

	HRESULT	hr;
	hr = pCaptureFilter->QueryInterface(__uuidof(IKsControl),
		(void**)ppKsControl);

	if (hr != S_OK)
	{
		return FALSE;
	}

	return TRUE;
}

void print_usage(const char *program)
{
    printf("usage: %s [command]\n", program);
    printf("  command: \n");
    printf("    loader: enter to loader mode\n");
    printf("    version: display firmware version\n");
    printf("    reboot: just reboot\n");
    printf("    (eptz cmds):\n");
    printf("       on\n");
    printf("       paused\n");
    printf("       off\n");
}

int main(int argc, char**argv)
{

    USHORT usVid, usPid;
    UCHAR controlId;
    DWORD command = 0;
    UCHAR buffer[60];
    ULONG retSize;
    bool get = false;
    DWORD *pCmd = (DWORD *)buffer;
    memset(buffer, 0, 60);

    if (argc != 2)
    {
        print_usage(argv[0]);
        return -1;
    }

    if ('v' == argv[1][0] )
    {
        controlId = 0x2;
        get = true;
    }
    else
    {
        if ('o' == argv[1][0] ||  'p' == argv[1][0])
        {
            controlId = 0xa;
            if ('p' == argv[1][0] )
            {
                command = 0x2;
            }
            else if ('n' == argv[1][1] )
            {
                command = 0x1;
            }
            else if ('f' == argv[1][1] )
            {
                command = 0x0;
            }
        }
        else {

            controlId = 0x1;
            if ('l' == argv[1][0] )
            {
                command = 0xFFFFFFFF;
            }
            else if ('r' == argv[1][0] )
            {
                command = 0xFFFFFFF1;
            }
            else
            {
                print_usage(argv[0]);
                return -1;
            }
        }
        *pCmd = command;
    }
    usVid = 0x04bb;
    usPid = 0x0551;
    UvcXuCommand(usVid, usPid, controlId, buffer, &retSize, get);
    printf("retSize %d\n", retSize);

    if (retSize == 60)
    {
        printf("version: %s\n",buffer);
    }
    return 0;
}
