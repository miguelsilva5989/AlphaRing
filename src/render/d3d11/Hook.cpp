#include "D3d11.h"

#include "common.h"

#include "global/Global.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace AlphaRing::Render::D3d11 {
    static std::atomic<unsigned> detected_split_viewports {0};
    static std::atomic<unsigned> native_copy_log_count {0};
    static std::atomic<unsigned> native_viewport_log_count {0};
    static std::atomic<bool> halo1_module_loaded {false};
    static std::atomic<unsigned> unloaded_non_halo_modules {0};
    static std::atomic<bool> native_render_requested {false};
    static std::atomic<bool> native_render_active {false};

    struct NativeTextureRecord {
        UINT original_width;
        UINT original_height;
        DXGI_FORMAT format;
        bool full_combined_target;
    };

    static const GUID native_texture_marker_guid {
            0x69d49e63,
            0x5779,
            0x4f41,
            {0xb2, 0x1b, 0x30, 0x37, 0x45, 0xb4, 0xd2, 0x0e}
    };

    static std::mutex native_render_mutex;
    static UINT native_base_width = 0;
    static UINT native_base_height = 0;
    static ID3D11Texture2D* native_mirror_source = nullptr;

    static void ResetNativeRenderStateLocked() {
        if (native_mirror_source) {
            native_mirror_source->Release();
            native_mirror_source = nullptr;
        }
        native_copy_log_count.store(0, std::memory_order_relaxed);
        native_viewport_log_count.store(0, std::memory_order_relaxed);
        native_base_width = 0;
        native_base_height = 0;
    }

    static void DeactivateNativeRender() {
        std::lock_guard<std::mutex> lock(native_render_mutex);
        native_render_active.store(false, std::memory_order_release);
        ResetNativeRenderStateLocked();
    }

    static void ActivateNativeRender() {
        if (NativeRenderActive() ||
            !halo1_module_loaded.load(std::memory_order_acquire) ||
            !native_render_requested.load(std::memory_order_acquire))
            return;

        std::lock_guard<std::mutex> lock(native_render_mutex);
        if (native_render_active.load(std::memory_order_acquire))
            return;

        ResetNativeRenderStateLocked();
        if (!Graphics()->pSwapChain) {
            LOG_ERROR("Native CE: main swap chain is unavailable");
            return;
        }

        ID3D11Texture2D* backbuffer = nullptr;
        const auto result = Graphics()->pSwapChain->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&backbuffer)
        );
        if (FAILED(result) || !backbuffer) {
            LOG_ERROR("Native CE: failed to inspect the main backbuffer: 0x{:x}", static_cast<unsigned>(result));
            return;
        }

        D3D11_TEXTURE2D_DESC desc {};
        backbuffer->GetDesc(&desc);
        backbuffer->Release();
        if (desc.Width == 0 || desc.Height < 2 || desc.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION / 2) {
            LOG_ERROR("Native CE: unsupported base resolution {}x{}", desc.Width, desc.Height);
            return;
        }

        native_base_width = desc.Width;
        native_base_height = desc.Height;
        native_render_active.store(true, std::memory_order_release);
        LOG_INFO(
                "Native CE: active base={}x{} combined={}x{} player={}x{}",
                native_base_width,
                native_base_height,
                native_base_width,
                native_base_height * 2,
                native_base_width,
                native_base_height
        );
    }

    void NotifyMccModuleLoaded(int title) {
        if (title != 0)
            return;

        halo1_module_loaded.store(true, std::memory_order_release);
        unloaded_non_halo_modules.store(0, std::memory_order_release);
        LOG_INFO("Native CE: Halo CE module preloaded");
    }

    void NotifyMccModuleUnloaded(int title) {
        if (title == 0) {
            halo1_module_loaded.store(false, std::memory_order_release);
            unloaded_non_halo_modules.store(0, std::memory_order_release);
            DeactivateNativeRender();
            LOG_INFO("Native CE: Halo CE module unloaded");
            return;
        }

        if (title < 1 || title > 6 || !halo1_module_loaded.load(std::memory_order_acquire))
            return;

        const unsigned bit = 1u << static_cast<unsigned>(title - 1);
        const unsigned unloaded = unloaded_non_halo_modules.fetch_or(bit, std::memory_order_acq_rel) | bit;
        if (unloaded == 0x3fu) {
            LOG_INFO("Native CE: Halo CE selected; activating before map resources are created");
            ActivateNativeRender();
        }
    }

    void SetNativeRenderEnabled(bool enabled) {
        if (!enabled && NativeRenderActive()) {
            LOG_WARNING("Native CE: cannot disable while Halo CE is active; unload Halo CE first");
            return;
        }

        MonitorSplit().native_ce_render = enabled;
        native_render_requested.store(enabled, std::memory_order_release);
        if (!enabled) {
            LOG_INFO("Native CE: disabled");
            return;
        }

        LOG_INFO("Native CE: armed for the next Halo CE selection");
    }

    bool NativeRenderActive() {
        return native_render_active.load(std::memory_order_acquire);
    }

    const char* NativeRenderStatus() {
        if (NativeRenderActive())
            return "Active";
        if (!native_render_requested.load(std::memory_order_acquire))
            return "Disabled";
        return "Armed for next Halo CE selection";
    }

    ID3D11Texture2D* AcquireNativeMirrorSource() {
        std::lock_guard<std::mutex> lock(native_render_mutex);
        if (native_mirror_source)
            native_mirror_source->AddRef();
        return native_mirror_source;
    }

    static bool IsNativeRenderFormat(DXGI_FORMAT format) {
        return format == static_cast<DXGI_FORMAT>(9) ||
               format == static_cast<DXGI_FORMAT>(19) ||
               format == static_cast<DXGI_FORMAT>(39) ||
               format == static_cast<DXGI_FORMAT>(90);
    }

    static bool TryRemapTextureDesc(
            const D3D11_TEXTURE2D_DESC& original,
            D3D11_TEXTURE2D_DESC& remapped,
            bool& full_combined_target
    ) {
        if (!NativeRenderActive() ||
            original.MipLevels != 1 ||
            original.ArraySize != 1 ||
            original.Height == 0 ||
            original.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION / 2 ||
            (original.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) == 0 ||
            !IsNativeRenderFormat(original.Format))
            return false;

        UINT base_width;
        UINT base_height;
        {
            std::lock_guard<std::mutex> lock(native_render_mutex);
            base_width = native_base_width;
            base_height = native_base_height;
        }
        if (base_width == 0 || base_height < 2 ||
            original.Width > base_width || original.Height > base_height)
            return false;

        const auto combined_aspect =
                static_cast<unsigned long long>(original.Width) * base_height ==
                static_cast<unsigned long long>(original.Height) * base_width;
        const auto player_aspect =
                static_cast<unsigned long long>(original.Width) * (base_height / 2) ==
                static_cast<unsigned long long>(original.Height) * base_width;
        if (!combined_aspect && !player_aspect)
            return false;

        remapped = original;
        remapped.Height = original.Height * 2;
        full_combined_target = original.Width == base_width && original.Height == base_height;
        return true;
    }

    static void RememberNativeTexture(
            ID3D11Texture2D* texture,
            const D3D11_TEXTURE2D_DESC& original,
            bool full_combined_target
    ) {
        if (!texture)
            return;

        if (!NativeRenderActive())
            return;

        const NativeTextureRecord marker {
                original.Width,
                original.Height,
                original.Format,
                full_combined_target,
        };
        if (FAILED(texture->SetPrivateData(native_texture_marker_guid, sizeof(marker), &marker))) {
            LOG_ERROR("Native CE: failed to mark remapped texture {:p}", static_cast<void*>(texture));
            return;
        }

        LOG_INFO(
                "Native CE: texture {:p} {}x{} -> {}x{} fmt={} combined={}",
                static_cast<void*>(texture),
                original.Width,
                original.Height,
                original.Width,
                original.Height * 2,
                static_cast<unsigned>(original.Format),
                full_combined_target
        );
    }

    static bool FindNativeTexture(ID3D11Texture2D* texture, NativeTextureRecord& record) {
        if (!texture || !NativeRenderActive())
            return false;

        UINT size = sizeof(record);
        return SUCCEEDED(texture->GetPrivateData(native_texture_marker_guid, &size, &record)) &&
               size == sizeof(record);
    }

    static bool FindNativeResource(ID3D11Resource* resource, NativeTextureRecord& record) {
        if (!resource || !NativeRenderActive())
            return false;

        ID3D11Texture2D* texture = nullptr;
        const auto result = resource->QueryInterface(
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture)
        );
        if (FAILED(result) || !texture)
            return false;

        const bool found = FindNativeTexture(texture, record);
        texture->Release();
        return found;
    }

    static bool GetCurrentNativeTarget(
            ID3D11DeviceContext* context,
            NativeTextureRecord& record,
            ID3D11Texture2D** texture_out
    ) {
        ID3D11RenderTargetView* render_target = nullptr;
        ID3D11DepthStencilView* depth_view = nullptr;
        context->OMGetRenderTargets(1, &render_target, &depth_view);

        ID3D11Texture2D* texture = nullptr;
        if (render_target) {
            ID3D11Resource* resource = nullptr;
            render_target->GetResource(&resource);
            if (resource) {
                resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
                resource->Release();
            }
        }
        if (!texture && depth_view) {
            ID3D11Resource* resource = nullptr;
            depth_view->GetResource(&resource);
            if (resource) {
                resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
                resource->Release();
            }
        }

        if (render_target)
            render_target->Release();
        if (depth_view)
            depth_view->Release();

        if (!FindNativeTexture(texture, record)) {
            if (texture)
                texture->Release();
            return false;
        }

        *texture_out = texture;
        return true;
    }

    static bool NearlyEqual(float a, float b) {
        return fabsf(a - b) <= 2.0f;
    }

    static bool IsTopSplitViewport(const D3D11_VIEWPORT& viewport, float width, float height) {
        return NearlyEqual(viewport.TopLeftX, 0.0f) &&
               NearlyEqual(viewport.TopLeftY, 0.0f) &&
               NearlyEqual(viewport.Width, width) &&
               NearlyEqual(viewport.Height, height * 0.5f);
    }

    static bool IsBottomSplitViewport(const D3D11_VIEWPORT& viewport, float width, float height) {
        return NearlyEqual(viewport.TopLeftX, 0.0f) &&
               NearlyEqual(viewport.TopLeftY, height * 0.5f) &&
               NearlyEqual(viewport.Width, width) &&
               NearlyEqual(viewport.Height, height * 0.5f);
    }

    static void TrackNativeCombinedTarget(
            ID3D11Texture2D* texture,
            const NativeTextureRecord& record,
            const D3D11_VIEWPORT& original_viewport,
            ID3D11DeviceContext* context
    ) {
        if (!record.full_combined_target || record.format != static_cast<DXGI_FORMAT>(90) || !texture)
            return;

        std::lock_guard<std::mutex> lock(native_render_mutex);
        if (native_mirror_source == texture)
            return;

        texture->AddRef();
        if (native_mirror_source)
            native_mirror_source->Release();
        native_mirror_source = texture;

        const unsigned log_index = native_viewport_log_count.fetch_add(1, std::memory_order_relaxed);
        if (log_index < 24) {
            LOG_INFO(
                    "Native CE: selected full target {:p} viewport={},{} {}x{} immediate={}",
                    static_cast<void*>(texture),
                    original_viewport.TopLeftX,
                    original_viewport.TopLeftY,
                    original_viewport.Width,
                    original_viewport.Height,
                    context == Graphics()->pContext
            );
        }
    }

    static void DetectSplitViewport(const D3D11_VIEWPORT& viewport) {
        if (!Graphics()->pSwapChain)
            return;

        DXGI_SWAP_CHAIN_DESC desc {};
        if (FAILED(Graphics()->pSwapChain->GetDesc(&desc)))
            return;

        const float width = static_cast<float>(desc.BufferDesc.Width);
        const float height = static_cast<float>(desc.BufferDesc.Height);
        if (width <= 0.0f || height <= 0.0f)
            return;

        if (IsTopSplitViewport(viewport, width, height)) {
            detected_split_viewports.fetch_or(1u, std::memory_order_relaxed);
        } else if (IsBottomSplitViewport(viewport, width, height)) {
            detected_split_viewports.fetch_or(2u, std::memory_order_relaxed);
        }
    }

    unsigned ConsumeDetectedSplitViewportMask() {
        return detected_split_viewports.exchange(0, std::memory_order_relaxed);
    }

    DefDetourFunction(HRESULT, __stdcall, CreateTexture2D,
                      ID3D11Device* device,
                      const D3D11_TEXTURE2D_DESC* desc,
                      const D3D11_SUBRESOURCE_DATA* initial_data,
                      ID3D11Texture2D** texture) {
        if (!desc)
            return ppOriginal_CreateTexture2D(device, desc, initial_data, texture);

        D3D11_TEXTURE2D_DESC remapped_desc {};
        bool full_combined_target = false;
        const bool remap = !initial_data && TryRemapTextureDesc(*desc, remapped_desc, full_combined_target);
        const auto result = ppOriginal_CreateTexture2D(
                device,
                remap ? &remapped_desc : desc,
                initial_data,
                texture
        );

        if (SUCCEEDED(result) && remap && texture && *texture)
            RememberNativeTexture(*texture, *desc, full_combined_target);
        return result;
    }

    DefDetourFunction(HRESULT, __stdcall, DrawIndexed, ID3D11DeviceContext* p_context,
                      const UINT IndexCount, const UINT StartIndexLocation, const INT  BaseVertexLocation) {
        if (AlphaRing::Global::Global()->wireframe) {
            Graphics()->SetWireframe();
#ifdef NEW_WIREFRAME
            D3D11_PRIMITIVE_TOPOLOGY topology;
            p_context->IAGetPrimitiveTopology(&topology);

            p_context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);

            p_context->IASetPrimitiveTopology(topology);
#endif
        }

        return ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);
    }

    DefDetourFunction(void, __stdcall, RSSetViewports, ID3D11DeviceContext* p_context,
                      UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
        if (p_context == Graphics()->pContext && NumViewports == 1 && pViewports)
            DetectSplitViewport(pViewports[0]);

        NativeTextureRecord record {};
        ID3D11Texture2D* target = nullptr;
        if (NativeRenderActive() &&
            pViewports && NumViewports <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE &&
            GetCurrentNativeTarget(p_context, record, &target)) {
            D3D11_VIEWPORT remapped[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            memcpy(remapped, pViewports, sizeof(D3D11_VIEWPORT) * NumViewports);
            for (UINT i = 0; i < NumViewports; ++i) {
                remapped[i].TopLeftY *= 2.0f;
                remapped[i].Height *= 2.0f;
            }

            if (NumViewports == 1)
                TrackNativeCombinedTarget(target, record, pViewports[0], p_context);
            target->Release();
            ppOriginal_RSSetViewports(p_context, NumViewports, remapped);
            return;
        }

        ppOriginal_RSSetViewports(p_context, NumViewports, pViewports);
    }

    DefDetourFunction(void, __stdcall, RSSetScissorRects, ID3D11DeviceContext* p_context,
                      UINT NumRects, const D3D11_RECT* pRects) {
        NativeTextureRecord record {};
        ID3D11Texture2D* target = nullptr;
        if (NativeRenderActive() &&
            pRects && NumRects <= D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE &&
            GetCurrentNativeTarget(p_context, record, &target)) {
            D3D11_RECT remapped[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            memcpy(remapped, pRects, sizeof(D3D11_RECT) * NumRects);
            for (UINT i = 0; i < NumRects; ++i) {
                remapped[i].top *= 2;
                remapped[i].bottom *= 2;
            }
            target->Release();
            ppOriginal_RSSetScissorRects(p_context, NumRects, remapped);
            return;
        }

        ppOriginal_RSSetScissorRects(p_context, NumRects, pRects);
    }

    DefDetourFunction(void, __stdcall, CopySubresourceRegion,
                      ID3D11DeviceContext* context,
                      ID3D11Resource* destination,
                      UINT destination_subresource,
                      UINT destination_x,
                      UINT destination_y,
                      UINT destination_z,
                      ID3D11Resource* source,
                      UINT source_subresource,
                      const D3D11_BOX* source_box) {
        NativeTextureRecord destination_record {};
        NativeTextureRecord source_record {};
        const bool destination_native = FindNativeResource(destination, destination_record);
        const bool source_native = FindNativeResource(source, source_record);

        if (destination_native && source_native) {
            D3D11_BOX remapped_box {};
            const D3D11_BOX* box = nullptr;
            if (source_box) {
                remapped_box = *source_box;
                remapped_box.top *= 2;
                remapped_box.bottom *= 2;
                box = &remapped_box;
            }

            const unsigned log_index = native_copy_log_count.fetch_add(1, std::memory_order_relaxed);
            if (log_index < 16) {
                LOG_INFO(
                        "Native CE: copy {}x{} -> {}x{} dst_y={} -> {} boxed={}",
                        source_record.original_width,
                        source_record.original_height,
                        destination_record.original_width,
                        destination_record.original_height,
                        destination_y,
                        destination_y * 2,
                        source_box != nullptr
                );
            }

            ppOriginal_CopySubresourceRegion(
                    context,
                    destination,
                    destination_subresource,
                    destination_x,
                    destination_y * 2,
                    destination_z,
                    source,
                    source_subresource,
                    box
            );
            return;
        }

        ppOriginal_CopySubresourceRegion(
                context,
                destination,
                destination_subresource,
                destination_x,
                destination_y,
                destination_z,
                source,
                source_subresource,
                source_box
        );
    }

    bool CreateHook() {
        return Hook::Detour({
            {GetFunction(23), CreateTexture2D, (void **) &ppOriginal_CreateTexture2D},
            {GetFunction(73), DrawIndexed, (void **) &ppOriginal_DrawIndexed},
            {GetFunction(105), RSSetViewports, (void **) &ppOriginal_RSSetViewports},
            {GetFunction(106), RSSetScissorRects, (void **) &ppOriginal_RSSetScissorRects},
            {GetFunction(107), CopySubresourceRegion, (void **) &ppOriginal_CopySubresourceRegion},
        });
    }
}
