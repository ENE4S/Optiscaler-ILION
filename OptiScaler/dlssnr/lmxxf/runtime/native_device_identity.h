#pragma once
#include <unknwn.h>
// Verified ReShade unwrap interface. Never equate devices by adapter LUID.
inline constexpr GUID NativeDeviceUnwrapId={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
// (AMDNR 0.3.4, P1-B) Streamline's interposer proxies answer this with the object they wrap. Marvel's Midnight Suns
// (UE 4.26) creates its D3D12 device through Streamline 1, so a texture made on that proxy device reports the native
// device ("codec device mismatch"). Asked after the ReShade step; only a clean answer is used and anything else keeps
// the identity as before, so every pair that compared equal before still does.
inline constexpr GUID NativeDeviceStreamlineBaseId={0xADEC44E2,0x61F0,0x45C3,{0xAD,0x9F,0x1B,0x37,0x37,0x92,0x84,0xFF}};
#define AMDNR_NATIVE_IDENTITY_STREAMLINE 1 // tests\034\com_identity_test.cpp: the Streamline cases run
inline IUnknown*NativeDeviceIdentity(IUnknown*object){
 if(!object)return nullptr;
 IUnknown*raw=nullptr;HRESULT hr=object->QueryInterface(NativeDeviceUnwrapId,reinterpret_cast<void**>(&raw));
 if(hr!=S_OK&&hr!=E_NOINTERFACE){if(raw)raw->Release();return nullptr;}
 if(hr==S_OK&&!raw)return nullptr;
 if(hr==E_NOINTERFACE&&raw){raw->Release();return nullptr;}
 IUnknown*start=raw?raw:object;
 IUnknown*identity=nullptr;
 IUnknown*base=nullptr;
 if(start->QueryInterface(NativeDeviceStreamlineBaseId,reinterpret_cast<void**>(&base))==S_OK&&base){
  if(FAILED(base->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))){if(identity)identity->Release();identity=nullptr;}
 }
 if(base)base->Release();
 HRESULT identity_hr=S_OK;
 if(!identity)identity_hr=start->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity));
 if(raw)raw->Release();
 if(FAILED(identity_hr)){if(identity)identity->Release();return nullptr;}
 return identity; // Caller owns one reference, including native-only case.
}
inline bool NativeSameDevice(IUnknown*a,IUnknown*b){
 IUnknown*x=NativeDeviceIdentity(a),*y=NativeDeviceIdentity(b);
 bool same=x&&y&&x==y;if(x)x->Release();if(y)y->Release();return same;
}
