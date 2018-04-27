// Copyright(c) 2017 POLYGONTEK
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http ://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Precompiled.h"
#include "Render/Render.h"
#include "RenderInternal.h"
#include "RBackEnd.h"

BE_NAMESPACE_BEGIN

// litVisAABB 와 viewFrustum 을 이용해서 light OBB 의 near, far 를 구한다
static bool RB_ComputeNearFar(const Vec3 &lightOrigin, const OBB &lightOBB, const AABB &litVisAABB, const Frustum &viewFrustum, float *dNear, float *dFar) {
    float dmin1, dmax1;
    float dmin2, dmax2;

    Vec3 lightDir = lightOBB.Axis()[0];
    float lightFar = lightOBB.Extents()[0] * 2.0f;

    litVisAABB.AxisProjection(lightDir, dmin1, dmax1);
    viewFrustum.AxisProjection(lightDir, dmin2, dmax2);

    *dNear = dmin1 - lightDir.Dot(lightOrigin);
    
    *dFar = Max(dmax1, dmax2) - lightDir.Dot(lightOrigin);
    *dFar = Min(*dFar, lightFar);

    if (*dFar <= 0.0f || *dNear >= *dFar) {
        return false;
    }

    return true;
}

// litVisAABB 와 viewFrustum 을 이용해서 light frustum 의 near 와 far 를 구한다
static bool RB_ComputeNearFar(const Frustum &lightFrustum, const AABB &litVisAABB, const Frustum &viewFrustum, float *dNear, float *dFar) {
    float dmin1, dmax1;
    float dmin2, dmax2;
    
    Vec3 lightDir = lightFrustum.GetAxis()[0];

    litVisAABB.AxisProjection(lightDir, dmin1, dmax1);
    viewFrustum.AxisProjection(lightDir, dmin2, dmax2);

    *dNear = Max(lightFrustum.GetNearDistance(), dmin1 - lightDir.Dot(lightFrustum.GetOrigin()) - 4.0f);
    *dFar = Min(lightFrustum.GetFarDistance(), Max(dmax1, dmax2) - lightDir.Dot(lightFrustum.GetOrigin()));	

    if (*dFar <= lightFrustum.GetNearDistance() || *dNear >= *dFar) {
        return false;
    }

    return true;
}

static void RB_AlignProjectionBounds(float &xmin, float &xmax, float &ymin, float &ymax, const Vec2 &size, float alignSize) {
    // [-1, +1] to [0, 1]
    xmin = (xmin + 1.0f) * 0.5f;
    ymin = (ymin + 1.0f) * 0.5f;
    xmax = (xmax + 1.0f) * 0.5f;
    ymax = (ymax + 1.0f) * 0.5f;

    xmin = Math::Floor(xmin * size.x / alignSize);
    ymin = Math::Floor(ymin * size.y / alignSize);

    xmax = xmin + r_shadowMapSize.GetFloat();
    ymax = ymin + r_shadowMapSize.GetFloat();

    float ax = alignSize / size.x;
    float ay = alignSize / size.y;

    xmin = xmin * ax;
    ymin = ymin * ay;
    xmax = xmax * ax;
    ymax = ymax * ay;
    
    //xmax = Min(xmax, 1.0f);
    //ymax = Min(ymax, 1.0f);

    // [0, 1] to [-1, +1]
    xmin = xmin * 2.0f - 1.0f;
    ymin = ymin * 2.0f - 1.0f;
    xmax = xmax * 2.0f - 1.0f;
    ymax = ymax * 2.0f - 1.0f;
}

static bool RB_ComputeShadowCropMatrix(const OBB &lightOBB, const OBB &shadowCasterOBB, const Frustum &viewFrustum, Mat4 &shadowCropMatrix) {
    // crop bounds 를 만든다
    AABB casterCropBounds, viewCropBounds;
    lightOBB.ProjectionBounds(viewFrustum, viewCropBounds);
    lightOBB.ProjectionBounds(shadowCasterOBB, casterCropBounds);

    // 두개의 crop bounds 의 교집합
    AABB cropBounds;
    cropBounds[0][LeftAxis] = (casterCropBounds[0][LeftAxis] > viewCropBounds[0][LeftAxis]) ? casterCropBounds[0][LeftAxis] : viewCropBounds[0][LeftAxis];
    cropBounds[1][LeftAxis] = (casterCropBounds[1][LeftAxis] < viewCropBounds[1][LeftAxis]) ? casterCropBounds[1][LeftAxis] : viewCropBounds[1][LeftAxis];
    cropBounds[0][UpAxis] = (casterCropBounds[0][UpAxis] > viewCropBounds[0][UpAxis]) ? casterCropBounds[0][UpAxis] : viewCropBounds[0][UpAxis];		
    cropBounds[1][UpAxis] = (casterCropBounds[1][UpAxis] < viewCropBounds[1][UpAxis]) ? casterCropBounds[1][UpAxis] : viewCropBounds[1][UpAxis];

    if (cropBounds[0][LeftAxis] > cropBounds[1][LeftAxis] || cropBounds[0][UpAxis] > cropBounds[1][UpAxis]) {
        return false;
    }

    float xmin = -cropBounds[1][LeftAxis];
    float xmax = -cropBounds[0][LeftAxis];
    float ymin =  cropBounds[0][UpAxis];
    float ymax =  cropBounds[1][UpAxis];

    R_Set2DCropMatrix(xmin, xmax, ymin, ymax, shadowCropMatrix);

    return true;
}

static bool RB_ComputeShadowCropMatrix(const OBB &lightOBB, const Sphere &viewSphere, Mat4 &shadowCropMatrix) {
    // Calculate projection bounds [-1 ~ +1] of viewSphere in lightOBB space
    AABB cropBounds;
    lightOBB.ProjectionBounds(viewSphere, cropBounds);

    float xmin = -cropBounds[1][LeftAxis];
    float xmax = -cropBounds[0][LeftAxis];
    float ymin =  cropBounds[0][UpAxis];
    float ymax =  cropBounds[1][UpAxis];

    if (r_shadowMapCropAlign.GetBool()) {
        float lengthPerTexel = viewSphere.Radius() * 2.0f / r_shadowMapSize.GetFloat();

        const Vec3 &e = lightOBB.Extents();
        RB_AlignProjectionBounds(xmin, xmax, ymin, ymax, Vec2(e.y, e.z) * 2.0f, lengthPerTexel);
    }

    R_Set2DCropMatrix(xmin, xmax, ymin, ymax, shadowCropMatrix);
    
    return true;
}

static bool RB_ComputeShadowCropMatrix(const Frustum &lightFrustum, const OBB &shadowCasterOBB, const Frustum &viewFrustum, Mat4 &shadowCropMatrix) {
    // crop bounds 를 만든다
    AABB casterCropBounds, viewCropBounds;
    lightFrustum.ProjectionBounds(viewFrustum, viewCropBounds);
    lightFrustum.ProjectionBounds(shadowCasterOBB, casterCropBounds);		

    // 두개의 crop bounds 의 교집합
    AABB cropBounds;
    cropBounds[0][LeftAxis] = (casterCropBounds[0][LeftAxis] > viewCropBounds[0][LeftAxis]) ? casterCropBounds[0][LeftAxis] : viewCropBounds[0][LeftAxis];
    cropBounds[1][LeftAxis] = (casterCropBounds[1][LeftAxis] < viewCropBounds[1][LeftAxis]) ? casterCropBounds[1][LeftAxis] : viewCropBounds[1][LeftAxis];
    cropBounds[0][UpAxis] = (casterCropBounds[0][UpAxis] > viewCropBounds[0][UpAxis]) ? casterCropBounds[0][UpAxis] : viewCropBounds[0][UpAxis];		
    cropBounds[1][UpAxis] = (casterCropBounds[1][UpAxis] < viewCropBounds[1][UpAxis]) ? casterCropBounds[1][UpAxis] : viewCropBounds[1][UpAxis];

    if (cropBounds[0][1] > cropBounds[1][1] || cropBounds[0][2] > cropBounds[1][2]) {
        return false;
    }

    float xmin = -cropBounds[1][LeftAxis];
    float xmax = -cropBounds[0][LeftAxis];
    float ymin =  cropBounds[0][UpAxis];
    float ymax =  cropBounds[1][UpAxis];

    R_Set2DCropMatrix(xmin, xmax, ymin, ymax, shadowCropMatrix);
    //BE_LOG(L"%f %f, %f %f\n", xmin, xmax, ymin, ymax);

    return true;
}

static bool RB_ComputeShadowCropMatrix(const Frustum &lightFrustum, const Frustum &viewFrustum, Mat4 &shadowCropMatrix) {
    AABB cropBounds;
    lightFrustum.ProjectionBounds(viewFrustum, cropBounds);

    float xmin = -cropBounds[1][LeftAxis];
    float xmax = -cropBounds[0][LeftAxis];
    float ymin =  cropBounds[0][UpAxis];
    float ymax =  cropBounds[1][UpAxis];

    R_Set2DCropMatrix(xmin, xmax, ymin, ymax, shadowCropMatrix);
    //BE_LOG(L"%f %f, %f %f\n", xmin, xmax, ymin, ymax);

    return true;
}

static bool RB_ShadowCubeMapFacePass(const VisibleLight *visibleLight, const Mat4 &lightViewMatrix, const Frustum &lightFrustum, const Frustum &viewFrustum, bool forceClear, int cubeMapFace) {
    const VisibleObject *prevSpace = nullptr;
    const SubMesh *     prevSubMesh = nullptr;
    const VisibleObject *skipObject = nullptr;
    const VisibleObject *entity2 = nullptr;
    const Material *    prevMaterial = nullptr;
    bool                firstDraw = true;

    backEnd.rbsurf.SetCurrentLight(visibleLight);
    
    for (int i = 0; i < visibleLight->shadowCasterSurfCount; i++) {
        const DrawSurf *surf = backEnd.drawSurfs[visibleLight->shadowCasterSurfFirst + i];

        if (surf->space == skipObject) {
            continue;
        }
            
        if (surf->material->GetFlags() & Material::NoShadow) {
            continue;
        }

        if (!(surf->material->GetSort() == Material::Sort::OpaqueSort || surf->material->GetSort() == Material::Sort::AlphaTestSort) &&
            !(surf->material->GetFlags() & Material::ForceShadow)) {
            continue;
        }

        bool isDifferentObject = surf->space != prevSpace;
        bool isDifferentSubMesh = prevSubMesh ? !surf->subMesh->IsShared(prevSubMesh) : true;
        bool isDifferentMaterial = surf->material != prevMaterial;
        bool isDifferentInstance = !(surf->flags & DrawSurf::UseInstancing) || isDifferentMaterial || isDifferentSubMesh || !prevSpace || prevSpace->def->state.flags != surf->space->def->state.flags || prevSpace->def->state.layer != surf->space->def->state.layer ? true : false;

        if (isDifferentObject || isDifferentSubMesh || isDifferentMaterial) {
            if (prevMaterial && isDifferentInstance) {
                backEnd.rbsurf.Flush();
            }

            backEnd.rbsurf.Begin(RBSurf::ShadowFlush, surf->material, surf->materialRegisters, surf->space);

            prevSubMesh = surf->subMesh;
            prevMaterial = surf->material;

            if (isDifferentObject) {
                prevSpace = surf->space;

                if (!(surf->space->def->state.flags & RenderObject::CastShadowsFlag)) {
                    continue;
                }

                OBB obb(surf->space->def->GetLocalAABB(), surf->space->def->state.origin, surf->space->def->state.axis);
                if (lightFrustum.CullOBB(obb)) {
                    skipObject = surf->space;
                    continue;
                }

                skipObject = nullptr;
            }
        }

        if (!surf->space->def->state.joints) {
            OBB obb(surf->subMesh->GetAABB() * surf->space->def->state.scale, surf->space->def->state.origin, surf->space->def->state.axis);
            if (lightFrustum.CullOBB(obb)) {
                continue;
            }
        }

        if (firstDraw) {
            firstDraw = false;

            backEnd.ctx->vscmRT->Begin();

            int vscmFaceWidth = backEnd.ctx->vscmRT->GetWidth() / 3;
            int vscmFaceHeight = backEnd.ctx->vscmRT->GetHeight() / 2;

            Rect faceRect;
            faceRect.x = vscmFaceWidth * (cubeMapFace >> 1);
            faceRect.y = vscmFaceHeight * (cubeMapFace & 1);
            faceRect.w = vscmFaceWidth;
            faceRect.h = vscmFaceHeight;

            rhi.SetViewport(faceRect);
            rhi.SetScissor(faceRect);

            if (!backEnd.ctx->vscmCleared[cubeMapFace]) {
                rhi.SetStateBits(RHI::DepthWrite);
                rhi.Clear(RHI::DepthBit, Color4::black, 1.0f, 0);
            } else {
                backEnd.ctx->vscmCleared[cubeMapFace] = false;
            }
        }

        if (surf->space != entity2) {
            if (!(surf->flags & DrawSurf::UseInstancing)) {
                backEnd.modelViewMatrix = lightViewMatrix * surf->space->def->GetObjectToWorldMatrix();
                backEnd.modelViewProjMatrix = backEnd.projMatrix * backEnd.modelViewMatrix;
            } else {
                backEnd.rbsurf.AddInstance(surf);
            }

            entity2 = surf->space;
        }

        backEnd.rbsurf.DrawSubMesh(surf->subMesh);
    }

    if (!firstDraw) {
        backEnd.rbsurf.Flush();

        backEnd.ctx->vscmRT->End();
    } else if (forceClear && !backEnd.ctx->vscmCleared[cubeMapFace]) {
        firstDraw = false;

        backEnd.ctx->vscmRT->Begin();

        int vscmFaceWidth = backEnd.ctx->vscmRT->GetWidth() / 3;
        int vscmFaceHeight = backEnd.ctx->vscmRT->GetHeight() / 2;

        Rect faceRect;
        faceRect.x = vscmFaceWidth * (cubeMapFace >> 1);
        faceRect.y = vscmFaceHeight * (cubeMapFace & 1);
        faceRect.w = vscmFaceWidth;
        faceRect.h = vscmFaceHeight;

        rhi.SetViewport(faceRect);
        rhi.SetScissor(faceRect);
                
        rhi.SetStateBits(RHI::DepthWrite);
        rhi.Clear(RHI::DepthBit, Color4::black, 1.0f, 0);

        backEnd.ctx->vscmRT->End();

        backEnd.ctx->vscmCleared[cubeMapFace] = true;
    }

    return !firstDraw;
}

static void RB_ShadowCubeMapPass(const VisibleLight *visibleLight, const Frustum &viewFrustum) {
    float zNear = r_shadowCubeMapZNear.GetFloat();
    float zFar = visibleLight->def->GetMajorRadius();
    float zRangeInv = 1.0f / (zFar - zNear);

    // Zeye 에서 depth 값을 구하기 위한 projection 행렬의 33, 43 성분을 다시 W(-Zeye) 로 나눈값
    backEnd.shadowProjectionDepth[0] = -zFar * zNear * zRangeInv;
    backEnd.shadowProjectionDepth[1] = zFar * zRangeInv;

    float fov = RAD2DEG(backEnd.ctx->vscmBiasedFov);
    R_SetPerspectiveProjectionMatrix(fov, fov, zNear, zFar, false, backEnd.shadowProjectionMatrix);

    Frustum lightFrustum;
    lightFrustum.SetOrigin(visibleLight->def->state.origin);
    float size = zFar * Math::Tan(Math::OneFourthPi);
    lightFrustum.SetSize(zNear, zFar, size, size);

    int shadowMapDraw = 0;

    Mat3 axis;

    Rect prevScissorRect = rhi.GetScissor();
    Mat4 prevProjMatrix = backEnd.projMatrix;
    Mat4 prevViewProjMatrix = backEnd.viewProjMatrix;
    backEnd.projMatrix = backEnd.shadowProjectionMatrix;

    for (int faceIndex = RHI::PositiveX; faceIndex <= RHI::NegativeZ; faceIndex++) {
        R_CubeMapFaceToAxis((RHI::CubeMapFace)faceIndex, axis);

        lightFrustum.SetAxis(axis);

        if (viewFrustum.CullFrustum(lightFrustum)) {
            continue;
        }

        Mat4 lightViewMatrix;
        R_SetViewMatrix(axis, visibleLight->def->state.origin, lightViewMatrix);

        backEnd.viewProjMatrix = backEnd.shadowProjectionMatrix * lightViewMatrix;

        backEnd.shadowMapOffsetFactor = visibleLight->def->state.shadowOffsetFactor;
        backEnd.shadowMapOffsetUnits = visibleLight->def->state.shadowOffsetUnits;

        rhi.SetDepthBias(backEnd.shadowMapOffsetFactor, backEnd.shadowMapOffsetUnits);

        if (RB_ShadowCubeMapFacePass(visibleLight, lightViewMatrix, lightFrustum, viewFrustum, true, faceIndex)) {
            shadowMapDraw++;
        }

        rhi.SetDepthBias(0.0f, 0.0f);
    }

    backEnd.projMatrix = prevProjMatrix;
    backEnd.viewProjMatrix = prevViewProjMatrix;

    rhi.SetScissor(prevScissorRect);
    rhi.SetViewport(backEnd.renderRect);

    backEnd.ctx->renderCounter.numShadowMapDraw += shadowMapDraw;
}

// TODO: cascade 별로 컬링해야함
static bool RB_ShadowMapPass(const VisibleLight *visibleLight, const Frustum &viewFrustum, int cascadeIndex, bool forceClear) {
    const VisibleObject *prevSpace = nullptr;
    const SubMesh *     prevSubMesh = nullptr;
    const Material *    prevMaterial = nullptr;
    bool                firstDraw = true;
    Rect                prevScissorRect;

    backEnd.rbsurf.SetCurrentLight(visibleLight);

    if (r_CSM_pancaking.GetBool()) {
        rhi.SetDepthClamp(true);
    }

    rhi.SetDepthBias(backEnd.shadowMapOffsetFactor, backEnd.shadowMapOffsetUnits);

    Mat4 prevProjMatrix = backEnd.projMatrix;
    Mat4 prevViewProjMatrix = backEnd.viewProjMatrix;

    backEnd.projMatrix = backEnd.shadowProjectionMatrix;
    backEnd.viewProjMatrix = backEnd.shadowProjectionMatrix * visibleLight->def->viewMatrix;
    
    for (int i = 0; i < visibleLight->shadowCasterSurfCount; i++) {
        const DrawSurf *surf = backEnd.drawSurfs[visibleLight->shadowCasterSurfFirst + i];

        if (!(surf->space->def->state.flags & RenderObject::CastShadowsFlag)) {
            continue;
        }

        if (surf->material->GetFlags() & Material::NoShadow) {
            continue;
        }

        if (!(surf->material->GetSort() == Material::Sort::OpaqueSort || surf->material->GetSort() == Material::Sort::AlphaTestSort) && 
            !(surf->material->GetFlags() & Material::ForceShadow)) {
            continue;
        }

        if (firstDraw) {
            firstDraw = false;

            backEnd.ctx->shadowMapRT->Begin(0, cascadeIndex);

            rhi.SetViewport(Rect(0, 0, backEnd.ctx->shadowMapRT->GetWidth(), backEnd.ctx->shadowMapRT->GetHeight()));

            prevScissorRect = rhi.GetScissor();
            rhi.SetScissor(Rect::empty);
            rhi.SetStateBits(RHI::DepthWrite);
            rhi.Clear(RHI::DepthBit, Color4::black, 1.0f, 0);
        }

        bool isDifferentObject = surf->space != prevSpace;
        bool isDifferentSubMesh = prevSubMesh ? !surf->subMesh->IsShared(prevSubMesh) : true;
        bool isDifferentMaterial = surf->material != prevMaterial;
        bool isDifferentInstance = !(surf->flags & DrawSurf::UseInstancing) || isDifferentMaterial || isDifferentSubMesh || !prevSpace || prevSpace->def->state.flags != surf->space->def->state.flags || prevSpace->def->state.layer != surf->space->def->state.layer ? true : false;

        if (isDifferentObject || isDifferentSubMesh || isDifferentMaterial) {
            if (prevMaterial && isDifferentInstance) {
                backEnd.rbsurf.Flush();
            }

            backEnd.rbsurf.Begin(RBSurf::ShadowFlush, surf->material, surf->materialRegisters, surf->space);

            prevSubMesh = surf->subMesh;
            prevMaterial = surf->material;

            if (isDifferentObject) {
                if (!(surf->flags & DrawSurf::UseInstancing)) {
                    backEnd.modelViewMatrix = visibleLight->def->viewMatrix * surf->space->def->GetObjectToWorldMatrix();
                    backEnd.modelViewProjMatrix = backEnd.shadowProjectionMatrix * backEnd.modelViewMatrix;
                }

                prevSpace = surf->space;
            }
        }

        if (surf->flags & DrawSurf::UseInstancing) {
            backEnd.rbsurf.AddInstance(surf);
        }

        backEnd.rbsurf.DrawSubMesh(surf->subMesh);
    }

    if (!firstDraw) {
        backEnd.rbsurf.Flush();

        backEnd.ctx->shadowMapRT->End();

        rhi.SetScissor(prevScissorRect);
        rhi.SetViewport(backEnd.renderRect);
    } else if (forceClear) {
        firstDraw = false;

        backEnd.ctx->shadowMapRT->Begin(0, cascadeIndex);

        rhi.SetViewport(Rect(0, 0, backEnd.ctx->shadowMapRT->GetWidth(), backEnd.ctx->shadowMapRT->GetHeight()));
        prevScissorRect = rhi.GetScissor();
        rhi.SetScissor(Rect::empty);
        
        rhi.SetStateBits(RHI::DepthWrite);
        rhi.Clear(RHI::DepthBit, Color4::black, 1.0f, 0);

        backEnd.ctx->shadowMapRT->End();

        rhi.SetScissor(prevScissorRect);
        rhi.SetViewport(backEnd.renderRect);
    }

    backEnd.projMatrix = prevProjMatrix;
    backEnd.viewProjMatrix = prevViewProjMatrix;

    rhi.SetDepthBias(0.0f, 0.0f);

    if (r_CSM_pancaking.GetBool()) {
        rhi.SetDepthClamp(false);
    }

    return !firstDraw;
}

static void RB_OrthogonalShadowMapPass(const VisibleLight *visibleLight, const Frustum &viewFrustum) {
    backEnd.shadowViewProjectionScaleBiasMatrix[0].SetZero();

    backEnd.shadowMapOffsetFactor = visibleLight->def->state.shadowOffsetFactor;
    backEnd.shadowMapOffsetUnits = visibleLight->def->state.shadowOffsetUnits;

    float dNear, dFar;
    if (!RB_ComputeNearFar(visibleLight->def->state.origin, visibleLight->def->worldOBB, visibleLight->shadowCasterAABB, viewFrustum, &dNear, &dFar)) {
        return;
    }

    R_SetOrthogonalProjectionMatrix(visibleLight->def->worldOBB.Extents()[1], visibleLight->def->worldOBB.Extents()[2], dNear, dFar, backEnd.shadowProjectionMatrix);

    if (r_optimizedShadowProjection.GetInteger() == 2) {
        Mat4 shadowCropMatrix;
        OBB lightOBB = visibleLight->def->worldOBB;

        lightOBB.SetCenter(visibleLight->def->state.origin + visibleLight->def->state.axis[0] * (dFar + dNear) * 0.5f);

        Vec3 extents = lightOBB.Extents();
        lightOBB.SetExtents(Vec3((dFar - dNear) * 0.5f, extents.y, extents.z));

        if (!RB_ComputeShadowCropMatrix(lightOBB, OBB(visibleLight->shadowCasterAABB), viewFrustum, shadowCropMatrix)) {
            return;
        }

        // crop matrix 를 곱해서 effective 'zoomed in' shadow view-projection matrix 를 만든다
        backEnd.shadowProjectionMatrix = shadowCropMatrix * backEnd.shadowProjectionMatrix;
    }

    backEnd.shadowMapFilterSize[0] = r_shadowMapFilterSize.GetFloat();

    static const Mat4 textureScaleBiasMatrix(Vec4(0.5, 0, 0, 0.5), Vec4(0, 0.5, 0, 0.5), Vec4(0, 0, 0.5, 0.5), Vec4(0.0, 0.0, 0.0, 1));
    backEnd.shadowViewProjectionScaleBiasMatrix[0] = textureScaleBiasMatrix * backEnd.shadowProjectionMatrix * visibleLight->def->viewMatrix;

    if (RB_ShadowMapPass(visibleLight, viewFrustum, 0, false)) {
        backEnd.ctx->renderCounter.numShadowMapDraw++;
    }
}

static void RB_ProjectedShadowMapPass(const VisibleLight *visibleLight, const Frustum &viewFrustum) {
    backEnd.shadowViewProjectionScaleBiasMatrix[0].SetZero();

    backEnd.shadowMapOffsetFactor = visibleLight->def->state.shadowOffsetFactor;
    backEnd.shadowMapOffsetUnits = visibleLight->def->state.shadowOffsetUnits;

    float dNear, dFar;
    if (!RB_ComputeNearFar(visibleLight->def->worldFrustum, visibleLight->shadowCasterAABB, viewFrustum, &dNear, &dFar)) {
        return;
    }

    float xFov = RAD2DEG(Math::ATan(visibleLight->def->worldFrustum.GetLeft(), dFar)) * 2.0f;
    float yFov = RAD2DEG(Math::ATan(visibleLight->def->worldFrustum.GetUp(), dFar)) * 2.0f;

    R_SetPerspectiveProjectionMatrix(xFov, yFov, dNear, dFar, false, backEnd.shadowProjectionMatrix);

    /*if (r_optimizedShadowProjection.GetInteger() > 0) {
        Mat4 shadowCropMatrix;
        Frustum lightFrustum = visibleLight->def->frustum;

        lightFrustum.MoveNearDistance(dNear);
        lightFrustum.MoveFarDistance(dFar);

        if (r_optimizedShadowProjection.GetInteger() == 2) {
            if (!RB_ComputeShadowCropMatrix(lightFrustum, OBB(visibleLight->shadowCasterAABB), viewFrustum, shadowCropMatrix)) {
                return;
            }
        } else {
            RB_ComputeShadowCropMatrix(lightFrustum, viewFrustum, shadowCropMatrix);
        }

        // crop matrix 를 곱해서 effective 'zoomed in' shadow view-projection matrix 를 만든다
        backEnd.shadowProjectionMatrix = shadowCropMatrix * backEnd.shadowProjectionMatrix;
    }*/

    backEnd.shadowMapFilterSize[0] = r_shadowMapFilterSize.GetFloat();

    static const Mat4 textureScaleBiasMatrix(Vec4(0.5, 0, 0, 0.5), Vec4(0, 0.5, 0, 0.5), Vec4(0, 0, 0.5, 0.5), Vec4(0.0, 0.0, 0.0, 1));
    backEnd.shadowViewProjectionScaleBiasMatrix[0] = textureScaleBiasMatrix * backEnd.shadowProjectionMatrix * visibleLight->def->viewMatrix;

    if (RB_ShadowMapPass(visibleLight, viewFrustum, 0, false)) {
        backEnd.ctx->renderCounter.numShadowMapDraw++;
    }
}

static bool RB_SingleCascadedShadowMapPass(const VisibleLight *visibleLight, const Frustum &splitViewFrustum, int cascadeIndex, bool forceClear) {
    // split 된 viewFrustum 일 수 있기 때문에 컬링 가능
    if (splitViewFrustum.CullAABB(visibleLight->litSurfsAABB)) {
        return false;
    }

    backEnd.shadowViewProjectionScaleBiasMatrix[cascadeIndex].SetZero();

    switch (cascadeIndex) {
    case 0:
        backEnd.shadowMapOffsetFactor = r_CSM_offsetFactor0.GetFloat();
        backEnd.shadowMapOffsetUnits = r_CSM_offsetUnits0.GetFloat();
        break;
    case 1:
        backEnd.shadowMapOffsetFactor = r_CSM_offsetFactor1.GetFloat();
        backEnd.shadowMapOffsetUnits = r_CSM_offsetUnits1.GetFloat();
        break;
    case 2:
        backEnd.shadowMapOffsetFactor = r_CSM_offsetFactor2.GetFloat();
        backEnd.shadowMapOffsetUnits = r_CSM_offsetUnits2.GetFloat();
        break;
    case 3:
    default:
        backEnd.shadowMapOffsetFactor = r_CSM_offsetFactor3.GetFloat();
        backEnd.shadowMapOffsetUnits = r_CSM_offsetUnits3.GetFloat();
        break;
    }

    float dNear, dFar;
    if (!RB_ComputeNearFar(visibleLight->def->state.origin, visibleLight->def->worldOBB, visibleLight->shadowCasterAABB, splitViewFrustum, &dNear, &dFar)) {
        return false;
    }

    R_SetOrthogonalProjectionMatrix(visibleLight->def->worldOBB.Extents()[1], visibleLight->def->worldOBB.Extents()[2], dNear, dFar, backEnd.shadowProjectionMatrix);

    if (r_optimizedShadowProjection.GetInteger() > 0) {
        Mat4 shadowCropMatrix;
        OBB lightOBB = visibleLight->def->worldOBB;

        lightOBB.SetCenter(visibleLight->def->state.origin + visibleLight->def->state.axis[0] * (dFar + dNear) * 0.5f);

        Vec3 extents = lightOBB.Extents();
        lightOBB.SetExtents(Vec3((dFar - dNear) * 0.5f, extents.y, extents.z));

        if (r_optimizedShadowProjection.GetInteger() == 2) {
            if (!RB_ComputeShadowCropMatrix(lightOBB, OBB(visibleLight->shadowCasterAABB), splitViewFrustum, shadowCropMatrix)) {
                return false;
            }
        } else {
            Sphere viewSphere = splitViewFrustum.ToMinimumSphere();
            float viewSize = viewSphere.Radius() * 2;
            float texelsPerCenti = r_shadowMapSize.GetFloat() / UnitToCenti(viewSize);
            backEnd.shadowMapFilterSize[cascadeIndex] = Max(r_shadowMapFilterSize.GetFloat() * texelsPerCenti, 1.0f);
                
            RB_ComputeShadowCropMatrix(lightOBB, viewSphere, shadowCropMatrix);
        }

        // crop matrix 를 곱해서 effective 'zoomed in' shadow view-projection matrix 를 만든다
        backEnd.shadowProjectionMatrix = shadowCropMatrix * backEnd.shadowProjectionMatrix;
    }

    static const Mat4 textureScaleBiasMatrix(Vec4(0.5, 0, 0, 0.5), Vec4(0, 0.5, 0, 0.5), Vec4(0, 0, 0.5, 0.5), Vec4(0.0, 0.0, 0.0, 1));
    backEnd.shadowViewProjectionScaleBiasMatrix[cascadeIndex] = textureScaleBiasMatrix * backEnd.shadowProjectionMatrix * visibleLight->def->viewMatrix;

    return RB_ShadowMapPass(visibleLight, splitViewFrustum, cascadeIndex, forceClear);
}

static void RB_CascadedShadowMapPass(const VisibleLight *visibleLight) {
    float dNear = backEnd.view->def->frustum.GetNearDistance();
    float dFar = r_CSM_maxDistance.GetFloat();
    int csmCount = r_CSM_count.GetInteger();

    R_ComputeSplitDistances(dNear, dFar, r_CSM_splitLamda.GetFloat(), csmCount, backEnd.csmDistances);

    // 각 split 뷰 프러스텀에 대하여 shadow map 생성
    for (int cascadeIndex = 0; cascadeIndex < csmCount; cascadeIndex++) {
        if (backEnd.csmDistances[cascadeIndex + 1] <= r_CSM_nonCachedDistance.GetFloat()) {
            backEnd.csmUpdateRatio[cascadeIndex] = 1.0f;
        } else {
            backEnd.csmUpdateRatio[cascadeIndex] = r_CSM_updateRatio.GetFloat() * (1.0f - ((float)cascadeIndex / (float)csmCount));
            Clamp(backEnd.csmUpdateRatio[cascadeIndex], 0.1f, 1.0f);
        }

        backEnd.csmUpdate[cascadeIndex] += backEnd.csmUpdateRatio[cascadeIndex];
        if (backEnd.csmUpdate[cascadeIndex] < 1.0f) {
            continue;
        }
        backEnd.csmUpdate[cascadeIndex] -= 1.0f;

        Frustum splitViewFrustum = backEnd.view->def->frustum;
        float dNear = backEnd.csmDistances[cascadeIndex];
        float dFar = backEnd.csmDistances[cascadeIndex + 1];

        if (r_CSM_blend.GetBool() && cascadeIndex > 0) {
            // FIXME
            dNear -= (backEnd.csmDistances[cascadeIndex] - backEnd.csmDistances[cascadeIndex - 1]);
        }

        splitViewFrustum.MoveNearDistance(dNear);
        splitViewFrustum.MoveFarDistance(dFar);

        if (RB_SingleCascadedShadowMapPass(visibleLight, splitViewFrustum, cascadeIndex, true)) {
            backEnd.ctx->renderCounter.numShadowMapDraw++;
        }
    }
}

void RB_ShadowPass(const VisibleLight *visibleLight) {
    if (visibleLight->def->state.type == RenderLight::PointLight) {
        RB_ShadowCubeMapPass(visibleLight, backEnd.view->def->frustum);
    } else if (visibleLight->def->state.type == RenderLight::SpotLight) {
        RB_ProjectedShadowMapPass(visibleLight, backEnd.view->def->frustum);
    } else if (visibleLight->def->state.type == RenderLight::DirectionalLight) {
        if ((visibleLight->def->state.flags & RenderLight::PrimaryLightFlag) && r_CSM_count.GetInteger() > 1) {
            RB_CascadedShadowMapPass(visibleLight);
        } else {
            RB_OrthogonalShadowMapPass(visibleLight, backEnd.view->def->frustum);
        }
    }
}

BE_NAMESPACE_END
