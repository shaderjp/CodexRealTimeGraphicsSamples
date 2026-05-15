#include "BistroScene.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <stdexcept>

using namespace DirectX;

namespace
{
    std::wstring ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return std::wstring();
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (size <= 0)
        {
            return std::wstring(value.begin(), value.end());
        }

        std::wstring result(static_cast<size_t>(size - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
        return result;
    }

    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return std::string();
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            std::string fallback;
            fallback.reserve(value.size());
            for (wchar_t c : value)
            {
                fallback.push_back(c >= 0 && c <= 0x7f ? static_cast<char>(c) : '?');
            }
            return fallback;
        }

        std::string result(static_cast<size_t>(size - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring CleanTextureName(const aiString& path)
    {
        std::filesystem::path p(ToWide(path.C_Str()));
        return p.filename().wstring();
    }

    bool Exists(const std::filesystem::path& path)
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    float Max3(float x, float y, float z)
    {
        return (std::max)(x, (std::max)(y, z));
    }

    float Luminance(const XMFLOAT3& color)
    {
        return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
    }

    XMFLOAT3 Subtract(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b)
    {
        return XMFLOAT3(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x);
    }

    float Length(const XMFLOAT3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    bool HasEnvironmentExtension(const std::filesystem::path& path)
    {
        const std::wstring extension = path.extension().wstring();
        return _wcsicmp(extension.c_str(), L".hdr") == 0 ||
            _wcsicmp(extension.c_str(), L".dds") == 0 ||
            _wcsicmp(extension.c_str(), L".tga") == 0 ||
            _wcsicmp(extension.c_str(), L".png") == 0 ||
            _wcsicmp(extension.c_str(), L".jpg") == 0 ||
            _wcsicmp(extension.c_str(), L".jpeg") == 0;
    }

    std::wstring ResolveTextureByName(const std::filesystem::path& textureRoot, const std::wstring& name, const wchar_t* suffix)
    {
        if (name.empty())
        {
            return std::wstring();
        }

        const std::wstring candidateStem = name + suffix;
        for (const wchar_t* extension : { L".dds", L".DDS", L".tga", L".TGA" })
        {
            const std::filesystem::path candidate = textureRoot / (candidateStem + extension);
            if (Exists(candidate))
            {
                return candidate.wstring();
            }
        }

        return std::wstring();
    }

    std::wstring ResolveTexturePath(const std::filesystem::path& assetRoot, const aiMaterial* material, aiTextureType type, const wchar_t* suffix)
    {
        aiString rawPath;
        if (material->GetTexture(type, 0, &rawPath) == AI_SUCCESS)
        {
            const std::wstring fileName = CleanTextureName(rawPath);
            const std::filesystem::path candidate = assetRoot / L"Textures" / fileName;
            if (Exists(candidate))
            {
                return candidate.wstring();
            }
        }

        aiString materialName;
        material->Get(AI_MATKEY_NAME, materialName);
        std::wstring name = ToWide(materialName.C_Str());
        if (name.rfind(L"MASTER_", 0) == 0)
        {
            name = name.substr(7);
        }

        std::wstring resolved = ResolveTextureByName(assetRoot / L"Textures", name, suffix);
        if (!resolved.empty())
        {
            return resolved;
        }

        return ResolveTextureByName(assetRoot / L"Textures", L"MASTER_" + name, suffix);
    }

    void PushRectLight(std::vector<Bistro::RtLight>& lights, const XMFLOAT3& center, const XMFLOAT3& edge0, const XMFLOAT3& edge1, const XMFLOAT3& radiance)
    {
        XMFLOAT3 half0(edge0.x * 0.5f, edge0.y * 0.5f, edge0.z * 0.5f);
        XMFLOAT3 half1(edge1.x * 0.5f, edge1.y * 0.5f, edge1.z * 0.5f);
        XMFLOAT3 corner(center.x - half0.x - half1.x, center.y - half0.y - half1.y, center.z - half0.z - half1.z);
        const float area = (std::max)(Length(Cross(edge0, edge1)), 0.0001f);

        Bistro::RtLight light{};
        light.positionArea = XMFLOAT4(corner.x, corner.y, corner.z, area);
        light.edge0Type = XMFLOAT4(edge0.x, edge0.y, edge0.z, 1.0f);
        light.edge1 = XMFLOAT4(edge1.x, edge1.y, edge1.z, 0.0f);
        light.radianceCdf = XMFLOAT4(radiance.x, radiance.y, radiance.z, 0.0f);
        lights.push_back(light);
    }

    void NormalizeLightCdf(std::vector<Bistro::RtLight>& lights)
    {
        std::vector<float> weights(lights.size());
        float totalWeight = 0.0f;
        for (size_t i = 0; i < lights.size(); ++i)
        {
            const XMFLOAT3 radiance(lights[i].radianceCdf.x, lights[i].radianceCdf.y, lights[i].radianceCdf.z);
            const float weight = (std::max)(0.0f, lights[i].positionArea.w * Luminance(radiance));
            weights[i] = weight;
            totalWeight += weight;
        }

        if (totalWeight <= 0.0f)
        {
            lights.clear();
            return;
        }

        float cumulative = 0.0f;
        for (size_t i = 0; i < lights.size(); ++i)
        {
            cumulative += weights[i] / totalWeight;
            lights[i].radianceCdf.w = (std::min)(cumulative, 1.0f);
        }
        lights.back().radianceCdf.w = 1.0f;
    }

    void ProcessNode(const aiScene* aiScene, const aiNode* node, const XMMATRIX& parentTransform, Bistro::Scene& scene)
    {
        const aiMatrix4x4& t = node->mTransformation;
        XMMATRIX local = XMMatrixSet(
            t.a1, t.b1, t.c1, t.d1,
            t.a2, t.b2, t.c2, t.d2,
            t.a3, t.b3, t.c3, t.d3,
            t.a4, t.b4, t.c4, t.d4);
        XMMATRIX world = local * parentTransform;
        XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

        for (uint32_t meshIndex = 0; meshIndex < node->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = aiScene->mMeshes[node->mMeshes[meshIndex]];
            const uint32_t baseVertex = static_cast<uint32_t>(scene.vertices.size());
            const uint32_t startIndex = static_cast<uint32_t>(scene.indices.size());

            for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
            {
                aiVector3D p = mesh->mVertices[i];
                aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0.0f, 1.0f, 0.0f);
                aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);
                aiVector3D tangent = mesh->HasTangentsAndBitangents() ? mesh->mTangents[i] : aiVector3D(1.0f, 0.0f, 0.0f);

                XMVECTOR position = XMVector3Transform(XMVectorSet(p.x, p.y, p.z, 1.0f), world);
                XMVECTOR normal = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(n.x, n.y, n.z, 0.0f), normalMatrix));
                XMVECTOR tng = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(tangent.x, tangent.y, tangent.z, 0.0f), normalMatrix));

                Bistro::Vertex vertex{};
                XMStoreFloat3(&vertex.position, position);
                XMStoreFloat3(&vertex.normal, normal);
                XMStoreFloat4(&vertex.tangent, XMVectorSet(XMVectorGetX(tng), XMVectorGetY(tng), XMVectorGetZ(tng), 1.0f));
                vertex.texcoord = XMFLOAT2(uv.x, uv.y);
                scene.vertices.push_back(vertex);

                scene.boundsMin.x = (std::min)(scene.boundsMin.x, vertex.position.x);
                scene.boundsMin.y = (std::min)(scene.boundsMin.y, vertex.position.y);
                scene.boundsMin.z = (std::min)(scene.boundsMin.z, vertex.position.z);
                scene.boundsMax.x = (std::max)(scene.boundsMax.x, vertex.position.x);
                scene.boundsMax.y = (std::max)(scene.boundsMax.y, vertex.position.y);
                scene.boundsMax.z = (std::max)(scene.boundsMax.z, vertex.position.z);
            }

            for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
            {
                const aiFace& face = mesh->mFaces[faceIndex];
                if (face.mNumIndices == 3)
                {
                    scene.indices.push_back(baseVertex + face.mIndices[0]);
                    scene.indices.push_back(baseVertex + face.mIndices[1]);
                    scene.indices.push_back(baseVertex + face.mIndices[2]);
                }
            }

            Bistro::DrawItem draw{};
            draw.indexCount = static_cast<uint32_t>(scene.indices.size()) - startIndex;
            draw.startIndex = startIndex;
            draw.baseVertex = 0;
            draw.materialIndex = mesh->mMaterialIndex;
            if (draw.indexCount > 0)
            {
                scene.draws.push_back(draw);
            }
        }

        for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
        {
            ProcessNode(aiScene, node->mChildren[childIndex], world, scene);
        }
    }
}

namespace Bistro
{
    std::wstring GetRepoRootFromExecutable(uint32_t levelsFromExeToRepoRoot)
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path path(exePath);
        path = path.parent_path();
        for (uint32_t i = 0; i < levelsFromExeToRepoRoot; ++i)
        {
            path = path.parent_path();
        }
        return path.wstring();
    }

    std::wstring FindAssetRoot(const std::wstring& sceneFbxName)
    {
        wchar_t envPath[32767] = {};
        const DWORD envSize = GetEnvironmentVariableW(L"BISTRO_ASSET_ROOT", envPath, static_cast<DWORD>(std::size(envPath)));
        if (envSize > 0 && envSize < std::size(envPath))
        {
            const std::filesystem::path candidate(envPath);
            if (Exists(candidate / sceneFbxName))
            {
                return candidate.wstring();
            }
        }

        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::filesystem::path searchRoot = std::filesystem::path(exePath).parent_path();
        for (int i = 0; i < 10 && !searchRoot.empty(); ++i)
        {
            for (const auto& candidate : { searchRoot / L"Bistro_v5_2" })
            {
                if (Exists(candidate / sceneFbxName))
                {
                    return candidate.wstring();
                }
            }
            searchRoot = searchRoot.parent_path();
        }

        throw std::runtime_error(ToUtf8(sceneFbxName) + " was not found. Set BISTRO_ASSET_ROOT or place Bistro_v5_2 at the repository root.");
    }

    std::wstring FindEnvironmentMapPath(const std::wstring& assetRoot)
    {
        wchar_t envPath[32767] = {};
        const DWORD envSize = GetEnvironmentVariableW(L"BISTRO_ENVIRONMENT_MAP", envPath, static_cast<DWORD>(std::size(envPath)));
        if (envSize > 0 && envSize < std::size(envPath))
        {
            const std::filesystem::path candidate(envPath);
            if (Exists(candidate) && HasEnvironmentExtension(candidate))
            {
                return candidate.wstring();
            }
        }

        const std::filesystem::path root(assetRoot);
        for (const wchar_t* name : { L"san_giuseppe_bridge_4k.hdr", L"Environment.hdr", L"environment.hdr", L"Sky.hdr", L"sky.hdr", L"Environment.dds", L"environment.dds" })
        {
            const std::filesystem::path candidate = root / name;
            if (Exists(candidate))
            {
                return candidate.wstring();
            }
        }

        for (const std::filesystem::path folder : { root / L"Textures", root / L"Environment", root / L"Environments" })
        {
            if (!Exists(folder))
            {
                continue;
            }
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
            {
                if (entry.is_regular_file() && HasEnvironmentExtension(entry.path()))
                {
                    const std::wstring stem = entry.path().stem().wstring();
                    if (stem.find(L"env") != std::wstring::npos || stem.find(L"Env") != std::wstring::npos || stem.find(L"sky") != std::wstring::npos || stem.find(L"Sky") != std::wstring::npos)
                    {
                        return entry.path().wstring();
                    }
                }
            }
        }

        return std::wstring();
    }

    Scene LoadScene(const std::wstring& assetRoot, const std::wstring& sceneFbxName)
    {
        std::filesystem::path root(assetRoot);
        std::filesystem::path fbxPath = root / sceneFbxName;
        if (!Exists(fbxPath))
        {
            throw std::runtime_error(ToUtf8(sceneFbxName) + " was not found in the selected asset root.");
        }

        Assimp::Importer importer;
        const uint32_t flags =
            aiProcess_Triangulate |
            aiProcess_ConvertToLeftHanded |
            aiProcess_CalcTangentSpace |
            aiProcess_GenSmoothNormals |
            aiProcess_ImproveCacheLocality |
            aiProcess_OptimizeMeshes |
            aiProcess_SortByPType;

        const aiScene* aiScene = importer.ReadFile(fbxPath.string(), flags);
        if (!aiScene || !aiScene->mRootNode)
        {
            throw std::runtime_error(importer.GetErrorString());
        }

        Scene scene;
        scene.assetRoot = assetRoot;
        scene.boundsMin = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
        scene.boundsMax = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        scene.materials.resize((std::max)(1u, aiScene->mNumMaterials));
        for (uint32_t i = 0; i < aiScene->mNumMaterials; ++i)
        {
            const aiMaterial* source = aiScene->mMaterials[i];
            Material& material = scene.materials[i];

            aiString name;
            source->Get(AI_MATKEY_NAME, name);
            material.name = ToWide(name.C_Str());
            material.textures[TextureSlotBaseColor] = ResolveTexturePath(root, source, aiTextureType_BASE_COLOR, L"_BaseColor");
            if (material.textures[TextureSlotBaseColor].empty())
            {
                material.textures[TextureSlotBaseColor] = ResolveTexturePath(root, source, aiTextureType_DIFFUSE, L"_BaseColor");
            }
            material.textures[TextureSlotNormal] = ResolveTexturePath(root, source, aiTextureType_NORMALS, L"_Normal");
            if (material.textures[TextureSlotNormal].empty())
            {
                material.textures[TextureSlotNormal] = ResolveTexturePath(root, source, aiTextureType_HEIGHT, L"_Normal");
            }
            material.textures[TextureSlotSpecular] = ResolveTexturePath(root, source, aiTextureType_SPECULAR, L"_Specular");
            material.textures[TextureSlotEmissive] = ResolveTexturePath(root, source, aiTextureType_EMISSIVE, L"_Emissive");

            aiColor4D baseColor{};
            if (aiGetMaterialColor(source, AI_MATKEY_BASE_COLOR, &baseColor) == AI_SUCCESS ||
                aiGetMaterialColor(source, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == AI_SUCCESS)
            {
                material.baseColorFactor = XMFLOAT4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
                material.alphaMasked = baseColor.a < 0.99f;
            }

            aiColor4D emissiveColor{};
            if (aiGetMaterialColor(source, AI_MATKEY_COLOR_EMISSIVE, &emissiveColor) == AI_SUCCESS)
            {
                const float emissiveMax = Max3(emissiveColor.r, emissiveColor.g, emissiveColor.b);
                material.emissiveFactor = XMFLOAT4(emissiveColor.r, emissiveColor.g, emissiveColor.b, emissiveMax > 0.0f ? 1.0f : 0.0f);
            }
            if (!material.textures[TextureSlotEmissive].empty() && Max3(material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z) <= 0.0f)
            {
                material.emissiveFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        ProcessNode(aiScene, aiScene->mRootNode, XMMatrixIdentity(), scene);
        if (scene.vertices.empty() || scene.indices.empty())
        {
            throw std::runtime_error(ToUtf8(sceneFbxName) + " did not contain renderable triangle meshes.");
        }

        return scene;
    }

    LightBuildResult BuildLightList(const Scene& scene, uint32_t maxEmissiveTriangleLights)
    {
        LightBuildResult result;
        std::vector<RtLight> emissiveCandidates;
        emissiveCandidates.reserve(1024);

        for (const DrawItem& draw : scene.draws)
        {
            if (draw.materialIndex >= scene.materials.size())
            {
                continue;
            }

            const Material& material = scene.materials[draw.materialIndex];
            const bool hasEmissiveTexture = !material.textures[TextureSlotEmissive].empty();
            const float materialEmission = Max3(material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z) * (std::max)(material.emissiveFactor.w, 0.0f);
            if (!hasEmissiveTexture && materialEmission <= 0.0f)
            {
                continue;
            }

            const XMFLOAT3 radiance(
                (std::max)(material.emissiveFactor.x, hasEmissiveTexture ? 0.35f : 0.0f),
                (std::max)(material.emissiveFactor.y, hasEmissiveTexture ? 0.35f : 0.0f),
                (std::max)(material.emissiveFactor.z, hasEmissiveTexture ? 0.35f : 0.0f));

            for (uint32_t i = 0; i + 2 < draw.indexCount; i += 3)
            {
                const uint32_t i0 = scene.indices[draw.startIndex + i + 0];
                const uint32_t i1 = scene.indices[draw.startIndex + i + 1];
                const uint32_t i2 = scene.indices[draw.startIndex + i + 2];
                if (i0 >= scene.vertices.size() || i1 >= scene.vertices.size() || i2 >= scene.vertices.size())
                {
                    continue;
                }

                const XMFLOAT3& p0 = scene.vertices[i0].position;
                const XMFLOAT3& p1 = scene.vertices[i1].position;
                const XMFLOAT3& p2 = scene.vertices[i2].position;
                const XMFLOAT3 edge0 = Subtract(p1, p0);
                const XMFLOAT3 edge1 = Subtract(p2, p0);
                const float area = Length(Cross(edge0, edge1)) * 0.5f;
                if (area <= 0.0001f)
                {
                    continue;
                }

                RtLight light{};
                light.positionArea = XMFLOAT4(p0.x, p0.y, p0.z, area);
                light.edge0Type = XMFLOAT4(edge0.x, edge0.y, edge0.z, 0.0f);
                light.edge1 = XMFLOAT4(edge1.x, edge1.y, edge1.z, 0.0f);
                light.radianceCdf = XMFLOAT4(radiance.x, radiance.y, radiance.z, 0.0f);
                emissiveCandidates.push_back(light);
            }
        }

        const size_t emissiveStride = emissiveCandidates.size() > maxEmissiveTriangleLights && maxEmissiveTriangleLights > 0
            ? (emissiveCandidates.size() + maxEmissiveTriangleLights - 1) / maxEmissiveTriangleLights
            : 1;
        for (size_t i = 0; i < emissiveCandidates.size(); i += emissiveStride)
        {
            result.lights.push_back(emissiveCandidates[i]);
        }
        result.emissiveTriangleCount = static_cast<uint32_t>(result.lights.size());

        const XMFLOAT3 extent(
            (std::max)(scene.boundsMax.x - scene.boundsMin.x, 1.0f),
            (std::max)(scene.boundsMax.y - scene.boundsMin.y, 1.0f),
            (std::max)(scene.boundsMax.z - scene.boundsMin.z, 1.0f));
        const XMFLOAT3 center(
            (scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
            (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
            (scene.boundsMin.z + scene.boundsMax.z) * 0.5f);

        const float ceilingY = scene.boundsMin.y + extent.y * 0.72f;
        PushRectLight(result.lights,
            XMFLOAT3(center.x, ceilingY, center.z - extent.z * 0.08f),
            XMFLOAT3(extent.x * 0.28f, 0.0f, 0.0f),
            XMFLOAT3(0.0f, 0.0f, extent.z * 0.10f),
            XMFLOAT3(1.0f, 0.78f, 0.50f));
        PushRectLight(result.lights,
            XMFLOAT3(center.x - extent.x * 0.18f, scene.boundsMin.y + extent.y * 0.46f, center.z + extent.z * 0.10f),
            XMFLOAT3(0.0f, 0.0f, extent.z * 0.16f),
            XMFLOAT3(0.0f, extent.y * 0.08f, 0.0f),
            XMFLOAT3(1.0f, 0.68f, 0.36f));
        PushRectLight(result.lights,
            XMFLOAT3(center.x + extent.x * 0.20f, scene.boundsMin.y + extent.y * 0.50f, center.z - extent.z * 0.16f),
            XMFLOAT3(extent.x * 0.16f, 0.0f, 0.0f),
            XMFLOAT3(0.0f, extent.y * 0.07f, 0.0f),
            XMFLOAT3(0.88f, 0.72f, 0.50f));
        result.proceduralAreaCount = 3;

        NormalizeLightCdf(result.lights);
        result.activeLightCount = static_cast<uint32_t>(result.lights.size());
        if (result.lights.empty())
        {
            result.lights.push_back(RtLight{});
        }
        return result;
    }
}
