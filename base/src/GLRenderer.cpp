#include "GLRenderer.h"
#include "GLProgram.h"
#include "Utility.h"
#include "gl/glew.h"
#include "Camera.h"
#include "ShaderConstantManager.h"
#include "TextureManager.h"

namespace Colours
{
    glm::vec3 yellow = glm::vec3(1, 1, 0);
    glm::vec3 orange = glm::vec3(0.89, 0.44, 0.1);
    glm::vec3 red = glm::vec3(1, 0, 0);
    glm::vec3 blue = glm::vec3(0, 0, 1);
}

GLRenderer::GLRenderer(uint32_t width, uint32_t height, float nearPlaneDistance, float farPlaneDistance)
    : m_width(width),
    m_height(height),
    m_farPlane(farPlaneDistance),
    m_nearPlane(nearPlaneDistance),
    m_randomNormalTexture(0),
    m_randomScalarTexture(0),
    m_depthTexture(0),
    m_normalTexture(0),
    m_positionTexture(0),
    m_colorTexture(0),
    m_postTexture(0),
    m_passProg(0),
    m_pointProg(0),
    m_directionalProg(0),
    m_diagnosticProg(0),
    m_postProg(0),
    m_currentProgram(0),
    m_pRenderCam(nullptr)
{
    m_invWidth = 1.0f / m_width;
    m_invHeight = 1.0f / m_height;

    ShaderConstantManager::Create();
}

GLRenderer::~GLRenderer()
{
    ShaderConstantManager::Destroy();
}

DrawableGeometry::DrawableGeometry()
    : vertex_array(),
    vertex_buffer(),
    index_buffer(),
    num_indices(),
    diffuse_tex(),
    normal_tex(),
    specular_tex()
{}

DrawableGeometry::~DrawableGeometry()
{
    glDeleteVertexArrays(1, &vertex_array);
    glDeleteBuffers(1, &vertex_buffer);
    glDeleteBuffers(1, &index_buffer);

    if (diffuse_tex != 0)
        TextureManager::GetSingleton()->Release(diffuse_tex);
    if (normal_tex != 0)
        TextureManager::GetSingleton()->Release(normal_tex);
    if (specular_tex != 0)
        TextureManager::GetSingleton()->Release(specular_tex);

    num_indices = 0;
    color = glm::vec3(0);
}

void GLRenderer::AddDrawableGeometryToList(const DrawableGeometry* geometry, RenderEnums::DrawListType listType)
{
    switch (listType)
    {
    case RenderEnums::OPAQUE_LIST:
        m_opaqueList.push_back(geometry);
        break;
    case RenderEnums::ALPHA_MASKED_LIST:
        m_alphaMaskedList.push_back(geometry);
        break;
    case RenderEnums::TRANSPARENT_LIST:
        m_transparentList.push_back(geometry);
        break;
    case RenderEnums::LIGHT_LIST:
        m_lightList.push_back(geometry);
        break;
    default:
        assert(true);   // Unknown list type!
    }
}

void GLRenderer::ApplyPerFrameShaderConstants()
{
    ShaderConstantManager* shaderConstantManager = ShaderConstantManager::GetSingleton();

    std::string perFrameConstantBuffer("PerFrame");
    shaderConstantManager->SetShaderConstant("ufFar", perFrameConstantBuffer, &m_farPlane);
    shaderConstantManager->SetShaderConstant("ufNear", perFrameConstantBuffer, &m_nearPlane);
    shaderConstantManager->SetShaderConstant("uiScreenHeight", perFrameConstantBuffer, &m_height);
    shaderConstantManager->SetShaderConstant("uiScreenWidth", perFrameConstantBuffer, &m_width);
    shaderConstantManager->SetShaderConstant("ufInvScrHeight", perFrameConstantBuffer, &m_invHeight);
    shaderConstantManager->SetShaderConstant("ufInvScrWidth", perFrameConstantBuffer, &m_invWidth);
    //glUniform1f(glGetUniformLocation(m_postProg, "ufMouseTexX"), mouse_dof_x*m_invWidth);
    //glUniform1f(glGetUniformLocation(m_postProg, "ufMouseTexY"), abs(static_cast<int32_t>(m_height)-mouse_dof_y)*m_invHeight);

    glm::mat4 view = m_pRenderCam->GetView();
    glm::mat4 persp = m_pRenderCam->GetPerspective();
    shaderConstantManager->SetShaderConstant("um4View", perFrameConstantBuffer, &view); 
    shaderConstantManager->SetShaderConstant("um4Persp", perFrameConstantBuffer, &persp);

    float zero = 0.0f;
    shaderConstantManager->SetShaderConstant("ufGlowmask", perFrameConstantBuffer, &zero);

    int32_t value = 0;
    shaderConstantManager->SetShaderConstant("ubBloomOn", perFrameConstantBuffer, &value/*m_bloomEnabled*/);
    shaderConstantManager->SetShaderConstant("ubToonOn", perFrameConstantBuffer, &value/*m_toonEnabled*/);
    shaderConstantManager->SetShaderConstant("ubDOFOn", perFrameConstantBuffer, &value/*m_DOFEnabled*/);
    shaderConstantManager->SetShaderConstant("ubDOFDebug", perFrameConstantBuffer, &value/*m_DOFDebug*/);

    value = m_displayType;
    shaderConstantManager->SetShaderConstant("uiDisplayType", perFrameConstantBuffer, &value);
}

void GLRenderer::ClearFramebuffer(RenderEnums::ClearType clearFlags)
{
    GLenum flags = 0;

    if (clearFlags & RenderEnums::CLEAR_COLOUR)
        flags |= GL_COLOR_BUFFER_BIT;
    if (clearFlags & RenderEnums::CLEAR_DEPTH)
        flags |= GL_DEPTH_BUFFER_BIT;
    if (clearFlags & RenderEnums::CLEAR_STENCIL)
        flags |= GL_STENCIL_BUFFER_BIT;

    glClear(flags);
}

void GLRenderer::ClearLists()
{
    m_opaqueList.clear();
    m_alphaMaskedList.clear();
    m_transparentList.clear();
    m_lightList.clear();
}

void GLRenderer::CreateBuffersAndUploadData(const Geometry& model, DrawableGeometry& out)
{
    // Create Vertex/Index buffers
    glCreateBuffers(1, &(out.vertex_buffer));
    glCreateBuffers(1, &(out.index_buffer));

    // Create vertex buffer storage and upload data
    glNamedBufferStorage(out.vertex_buffer, model.vertices.size() * sizeof(Vertex), &model.vertices[0], 0); // This is a static buffer that may not be mapped or written CPU-side, so no extra flags.

    // Create vertex buffer storage and upload data
    out.num_indices = model.indices.size();
    glNamedBufferStorage(out.index_buffer, out.num_indices * sizeof(GLuint), &model.indices[0], 0);
}

void GLRenderer::DrawAlphaMaskedList()
{
    glDepthMask(GL_FALSE);
    glDepthMask(GL_TRUE);
}

void GLRenderer::DrawGeometry(const DrawableGeometry* geom)
{
    assert(m_currentProgram != nullptr);
    m_currentProgram->CommitTextureBindings();
    m_currentProgram->CommitConstantBufferChanges();

    glBindVertexArray(geom->vertex_array);
    glDrawElements(GL_TRIANGLES, geom->num_indices, GL_UNSIGNED_INT, 0);
}

void GLRenderer::drawLight(glm::vec3 pos, float strength)
{
    glm::vec4 light = m_pRenderCam->GetView() * glm::vec4(pos, 1.0);
    if ((light.z - strength) > -m_nearPlane) // strength = radius.
    {
        return;
    }
    light.w = strength;
    m_currentProgram->SetShaderConstant("uf4Light", light);
    m_currentProgram->SetShaderConstant("ufLightIl", strength);

    //glm::vec4 left = vp * glm::vec4(pos + radius*m_pRenderCam->start_left, 1.0);
    //glm::vec4 up = vp * glm::vec4(pos + radius*m_pRenderCam->up, 1.0);
    //glm::vec4 center = vp * glm::vec4(pos, 1.0);

    //left = sc * left;
    //up = sc * up;
    //center = sc * center;

    //left /= left.w;
    //up /= up.w;
    //center /= center.w;

    //float hw = glm::distance(left, center);
    //float hh = glm::distance(up, center);

    //float r = (hh > hw) ? hh : hw;

    //float x = center.x - r;
    //float y = center.y - r;

    //glScissor(x, y, 2 * r, 2 * r);
    RenderQuad();
}

void GLRenderer::DrawLightList()
{
    SetShaderProgram(m_pointProg);
    SetTexturesForFullScreenPass();

    m_pointProg->SetTexture("u_Colortex", m_colorTexture);
    m_pointProg->SetShaderConstant("uf3LightCol", Colours::yellow);
    glDepthMask(GL_FALSE);
    drawLight(glm::vec3(5.4, -0.5, 3.0), 1.0);
    drawLight(glm::vec3(0.2, -0.5, 3.0), 1.0);
    m_pointProg->SetShaderConstant("uf3LightCol", Colours::orange);
    drawLight(glm::vec3(5.4, -2.5, 3.0), 1.0);
    drawLight(glm::vec3(0.2, -2.5, 3.0), 1.0);
    m_pointProg->SetShaderConstant("uf3LightCol", Colours::yellow);
    drawLight(glm::vec3(5.4, -4.5, 3.0), 1.0);
    drawLight(glm::vec3(0.2, -4.5, 3.0), 1.0);

    m_pointProg->SetShaderConstant("uf3LightCol", Colours::red);
    drawLight(glm::vec3(2.5, -1.2, 0.5), 2.5);

    m_pointProg->SetShaderConstant("uf3LightCol", Colours::blue);
    drawLight(glm::vec3(2.5, -5.0, 4.2), 2.5);
    glDepthMask(GL_TRUE);
}

void GLRenderer::DrawOpaqueList()
{
    glm::mat4 inverseView = m_pRenderCam->GetInverseView();
    SetShaderProgram(m_passProg);

    for (uint32_t i = 0; i < m_opaqueList.size(); ++i)
    {
        glm::mat4 inverse_transposed = glm::transpose(m_opaqueList[i]->inverseModelMat * inverseView);
        m_passProg->SetShaderConstant("um4Model", m_opaqueList[i]->modelMat);
        m_passProg->SetShaderConstant("um4InvTrans", inverse_transposed);
        m_passProg->SetShaderConstant("uf3Color", m_opaqueList[i]->color);

        m_passProg->SetTexture("t2DDiffuse", m_opaqueList[i]->diffuse_tex);
        m_passProg->SetTexture("t2DNormal", m_opaqueList[i]->normal_tex);
        m_passProg->SetTexture("t2DSpecular", m_opaqueList[i]->specular_tex);

        DrawGeometry(m_opaqueList[i]);
    }
    glBindVertexArray(0);
}

void GLRenderer::DrawTransparentList()
{

}

void GLRenderer::EndActiveFramebuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLRenderer::InitFramebuffers()
{
    glCreateTextures(GL_TEXTURE_2D, 1, &m_depthTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_normalTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_positionTexture);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_colorTexture);

    glEnable(GL_FRAMEBUFFER_SRGB);

    //Set up depth texture
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_depthTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_depthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(m_depthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_DEPTH_COMPONENT);
    glTextureStorage2D(m_depthTexture, 1, GL_DEPTH_COMPONENT32, m_width, m_height);
    glTextureSubImage2D(m_depthTexture, 0, 0, 0, m_width, m_height, GL_DEPTH_COMPONENT, GL_FLOAT, 0);

    //Set up normal texture
    glTextureParameteri(m_normalTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_normalTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_normalTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(m_normalTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(m_normalTexture, 1, GL_RGBA8, m_width, m_height);
    glTextureSubImage2D(m_normalTexture, 0, 0, 0, m_width, m_height, GL_RGBA, GL_FLOAT, 0);

    //Set up position texture
    glTextureParameteri(m_positionTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_positionTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_positionTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(m_positionTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(m_positionTexture, 1, GL_RGBA8, m_width, m_height);
    glTextureSubImage2D(m_positionTexture, 0, 0, 0, m_width, m_height, GL_RGBA, GL_FLOAT, 0);

    //Set up color texture
    glTextureParameteri(m_colorTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_colorTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_colorTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(m_colorTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(m_colorTexture, 1, GL_RGBA8, m_width, m_height);
    glTextureSubImage2D(m_colorTexture, 0, 0, 0, m_width, m_height, GL_RGBA, GL_FLOAT, 0);

    GLType_uint fbo = 0;
    glCreateFramebuffers(1, &fbo);

    GLType_int normal_loc;
    if (!m_passProg->GetOutputBindLocation("out_f4Normal", reinterpret_cast<GLType_uint&>(normal_loc)))
        assert(false);
    GLType_int position_loc;
    if (!m_passProg->GetOutputBindLocation("out_f4Position", reinterpret_cast<GLType_uint&>(position_loc)))
        assert(false);
    GLType_int color_loc;
    if (!m_passProg->GetOutputBindLocation("out_f4Colour", reinterpret_cast<GLType_uint&>(color_loc)))
        assert(false);

    GLenum draws[3];
    draws[normal_loc] = GL_COLOR_ATTACHMENT0;
    draws[position_loc] = GL_COLOR_ATTACHMENT1;
    draws[color_loc] = GL_COLOR_ATTACHMENT2;
    glNamedFramebufferDrawBuffers(fbo, 3, draws);

    // attach the texture to FBO depth attachment point
    glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, m_depthTexture, 0);
    glNamedFramebufferTexture(fbo, draws[normal_loc], m_normalTexture, 0);
    glNamedFramebufferTexture(fbo, draws[position_loc], m_positionTexture, 0);
    glNamedFramebufferTexture(fbo, draws[color_loc], m_colorTexture, 0);

    // check FBO status
    GLenum FBOstatus = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    if (FBOstatus == GL_FRAMEBUFFER_COMPLETE)
        m_FBO.push_back(fbo);
    else
        assert(false);

    //Post Processing buffer!
    //Set up post texture
    glCreateTextures(GL_TEXTURE_2D, 1, &m_postTexture);
    glTextureParameteri(m_postTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_postTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameterf(m_postTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameterf(m_postTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(m_postTexture, 1, GL_SRGB8, m_width, m_height);
    glTextureSubImage2D(m_postTexture, 0, 0, 0, m_width, m_height, GL_RGB, GL_FLOAT, 0);

    // create a framebuffer object
    fbo = 0;
    glCreateFramebuffers(1, &fbo);

    if (!m_directionalProg->GetOutputBindLocation("out_f4Colour", reinterpret_cast<GLType_uint&>(color_loc)))
        assert(false);
    GLenum draw[1];
    draw[color_loc] = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(fbo, 1, draw);

    // attach the texture to FBO depth attachment point
    glNamedFramebufferTexture(fbo, draw[color_loc], m_postTexture, 0);

    // check FBO status
    FBOstatus = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    if (FBOstatus == GL_FRAMEBUFFER_COMPLETE)
        m_FBO.push_back(fbo);
    else
        assert(false);
}

void GLRenderer::Initialize(const Camera* renderCamera)
{
    InitNoise();
    InitShaders();
    InitFramebuffers();
    InitQuad();
    InitSphere();

    m_pRenderCam = const_cast<Camera*>(renderCamera);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
}

void GLRenderer::InitNoise()
{
    const char * rand_norm_png = "../res/random_normal.png";
    const char * rand_png = "../res/random.png";

    m_randomNormalTexture = TextureManager::GetSingleton()->Acquire(rand_norm_png);
    glTextureParameteri(m_randomNormalTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_randomNormalTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_randomNormalTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameterf(m_randomNormalTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);

    m_randomScalarTexture = TextureManager::GetSingleton()->Acquire(rand_png);
    glTextureParameteri(m_randomScalarTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_randomScalarTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameterf(m_randomScalarTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameterf(m_randomScalarTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void GLRenderer::InitQuad()
{
    Geometry quad;

    quad.vertices.push_back(Vertex(glm::vec3(-1, 1, 0), glm::vec3(-1, 1, 0), glm::vec2(0, 1)));
    quad.vertices.push_back(Vertex(glm::vec3(-1, -1, 0), glm::vec3(-1, 1, 0), glm::vec2(0, 0)));
    quad.vertices.push_back(Vertex(glm::vec3(1, -1, 0), glm::vec3(-1, 1, 0), glm::vec2(1, 0)));
    quad.vertices.push_back(Vertex(glm::vec3(1, 1, 0), glm::vec3(-1, 1, 0), glm::vec2(1, 1)));

    quad.indices.push_back(0);
    quad.indices.push_back(1);
    quad.indices.push_back(2);
    quad.indices.push_back(0);
    quad.indices.push_back(2);
    quad.indices.push_back(3);

    CreateBuffersAndUploadData(quad, m_QuadGeometry);

    // Quad vertex specification
    glGenVertexArrays(1, &(m_QuadGeometry.vertex_array));
    glBindVertexArray(m_QuadGeometry.vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadGeometry.vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadGeometry.index_buffer);
    glVertexAttribPointer(quad_attributes::POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glVertexAttribPointer(quad_attributes::TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texcoord)));
    glEnableVertexAttribArray(quad_attributes::POSITION);
    glEnableVertexAttribArray(quad_attributes::TEXCOORD);

    glBindVertexArray(0);
}

void GLRenderer::InitShaders()
{
    const char * pass_vert = "../res/shaders/pass.vert";
    const char * shade_vert = "../res/shaders/shade.vert";
    const char * post_vert = "../res/shaders/post.vert";

    const char * pass_frag = "../res/shaders/pass.frag";
    const char * diagnostic_frag = "../res/shaders/diagnostic.frag";
    const char * ambient_frag = "../res/shaders/ambient.frag";
    const char * point_frag = "../res/shaders/point.frag";
    const char * post_frag = "../res/shaders/post.frag";

    std::vector<std::pair<std::string, RenderEnums::RenderProgramStage>> shaderSourceAndStagePair;
    std::map<std::string, GLType_uint> meshAttributeBindIndices, quadAttributeBindIndices, outputBindIndices;

    meshAttributeBindIndices["in_f3Position"] = mesh_attributes::POSITION;
    meshAttributeBindIndices["in_f3Normal"] = mesh_attributes::NORMAL;
    meshAttributeBindIndices["in_f2Texcoord"] = mesh_attributes::TEXCOORD;
    meshAttributeBindIndices["in_f3Tangent"] = mesh_attributes::TANGENT;

    quadAttributeBindIndices["in_f3Position"] = quad_attributes::POSITION;
    quadAttributeBindIndices["in_f2Texcoord"] = quad_attributes::TEXCOORD;

    outputBindIndices["out_f4Colour"] = 0;
    outputBindIndices["out_f4Normal"] = 1;
    outputBindIndices["out_f4Position"] = 2;

    shaderSourceAndStagePair.clear();
    shaderSourceAndStagePair.push_back(std::make_pair(pass_vert, RenderEnums::VERT));
    shaderSourceAndStagePair.push_back(std::make_pair(pass_frag, RenderEnums::FRAG));
    m_passProg = new GLProgram(RenderEnums::RENDER_PROGRAM, shaderSourceAndStagePair, meshAttributeBindIndices, outputBindIndices);

    shaderSourceAndStagePair.clear();
    shaderSourceAndStagePair.push_back(std::make_pair(shade_vert, RenderEnums::VERT));
    shaderSourceAndStagePair.push_back(std::make_pair(diagnostic_frag, RenderEnums::FRAG));
    m_diagnosticProg = new GLProgram(RenderEnums::RENDER_PROGRAM, shaderSourceAndStagePair, quadAttributeBindIndices, outputBindIndices);

    shaderSourceAndStagePair[1] = std::make_pair(ambient_frag, RenderEnums::FRAG);
    m_directionalProg = new GLProgram(RenderEnums::RENDER_PROGRAM, shaderSourceAndStagePair, quadAttributeBindIndices, outputBindIndices);

    shaderSourceAndStagePair[1] = std::make_pair(point_frag, RenderEnums::FRAG);
    m_pointProg = new GLProgram(RenderEnums::RENDER_PROGRAM, shaderSourceAndStagePair, quadAttributeBindIndices, outputBindIndices);

    shaderSourceAndStagePair.clear();
    shaderSourceAndStagePair.push_back(std::make_pair(post_vert, RenderEnums::VERT));
    shaderSourceAndStagePair.push_back(std::make_pair(post_frag, RenderEnums::FRAG));
    m_postProg = new GLProgram(RenderEnums::RENDER_PROGRAM, shaderSourceAndStagePair, quadAttributeBindIndices, outputBindIndices);
}

void GLRenderer::InitSphere()
{
    Geometry sphere;

    const uint32_t divisor = 10;
    const float inverseDivisor = 1.0f / divisor;
    const float pi = 3.1415926f;
    float thetaAdvance = 2 * pi * inverseDivisor;
    float phiAdvance = pi * inverseDivisor;

    for (uint32_t i = 0; i <= divisor; ++i)   // theta
    {
        for (uint32_t j = 0; j <= divisor; ++j)  // phi
        {
            glm::vec3 positionNormal(sin(i * thetaAdvance) * sin(j * phiAdvance), cos(j * phiAdvance), cos(i * thetaAdvance) * sin(j * phiAdvance));
            sphere.vertices.push_back(Vertex(positionNormal, positionNormal, glm::vec2(i * inverseDivisor, j * inverseDivisor)));

            if ((i < divisor) && (j < divisor))
            {
                sphere.indices.push_back(j);
                sphere.indices.push_back(((i + 1) * divisor + j));
                sphere.indices.push_back(((i + 1) * divisor + (j + 1)));
                sphere.indices.push_back(j);
                sphere.indices.push_back(((i + 1) * divisor + (j + 1)));
                sphere.indices.push_back(j + 1);
            }
        }
    }

    CreateBuffersAndUploadData(sphere, m_SphereGeometry);

    // Sphere vertex specification
    glGenVertexArrays(1, &(m_SphereGeometry.vertex_array));
    glBindVertexArray(m_SphereGeometry.vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, m_SphereGeometry.vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereGeometry.index_buffer);
    glVertexAttribPointer(mesh_attributes::POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glVertexAttribPointer(mesh_attributes::NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));
    glVertexAttribPointer(mesh_attributes::TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, texcoord)));
    glEnableVertexAttribArray(mesh_attributes::POSITION);
    glEnableVertexAttribArray(mesh_attributes::NORMAL);
    glEnableVertexAttribArray(mesh_attributes::TEXCOORD);

    glBindVertexArray(0);
}

void GLRenderer::MakeDrawableModel(const Geometry& model, DrawableGeometry& out, const glm::mat4& modelMatrix)
{
    CreateBuffersAndUploadData(model, out);

    // Vertex specification
    glGenVertexArrays(1, &(out.vertex_array));
    glBindVertexArray(out.vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, out.vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.index_buffer);
    glVertexAttribPointer(mesh_attributes::POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);
    glVertexAttribPointer(mesh_attributes::NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, normal)));
    glVertexAttribPointer(mesh_attributes::TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, texcoord)));
    glVertexAttribPointer(mesh_attributes::TANGENT, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, tangent)));
    glEnableVertexAttribArray(mesh_attributes::POSITION);
    glEnableVertexAttribArray(mesh_attributes::NORMAL);
    glEnableVertexAttribArray(mesh_attributes::TEXCOORD);
    glEnableVertexAttribArray(mesh_attributes::TANGENT);

    // Unplug Vertex Array
    glBindVertexArray(0);

    out.diffuse_tex = TextureManager::GetSingleton()->Acquire(model.diffuse_texpath);
    out.normal_tex = TextureManager::GetSingleton()->Acquire(model.normal_texpath);
    out.specular_tex = TextureManager::GetSingleton()->Acquire(model.specular_texpath);

    out.modelMat = modelMatrix;
    out.inverseModelMat = glm::inverse(out.modelMat);
    out.color = model.color;
}

void GLRenderer::Render()
{
    ApplyPerFrameShaderConstants();

    // GBuffer Pass
    SetFramebufferActive(RenderEnums::GBUFFER_FRAMEBUFFER);
    ClearFramebuffer(RenderEnums::CLEAR_ALL);
    DrawOpaqueList();
    DrawAlphaMaskedList();

    // Lighting Pass
    SetFramebufferActive(RenderEnums::LIGHTING_FRAMEBUFFER);
    ClearFramebuffer(RenderEnums::CLEAR_ALL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    DrawLightList();
    glDisable(GL_BLEND);
    RenderDirectionalAndAmbientLighting();
    EndActiveFramebuffer();

    // Post Process Pass
    ClearFramebuffer(RenderEnums::CLEAR_ALL);
    glDisable(GL_DEPTH_TEST);
    if (m_displayType != RenderEnums::DISPLAY_TOTAL)
        RenderFramebuffers();
    else
        RenderPostProcessEffects();
    glEnable(GL_DEPTH_TEST);
}

void GLRenderer::RenderDirectionalAndAmbientLighting()
{
    glm::vec4 dir_light(0.0, 1.0, 1.0, 0.0);
    dir_light = m_pRenderCam->GetView() * dir_light;
    dir_light = glm::normalize(dir_light);
    dir_light.w = 1.0f; // strength
    glm::vec3 ambient(0.04f);

    SetShaderProgram(m_directionalProg);
    SetTexturesForFullScreenPass();
    m_directionalProg->SetTexture("u_Colortex", m_colorTexture);
    m_directionalProg->SetShaderConstant("uf4DirecLightDir", dir_light);
    m_directionalProg->SetShaderConstant("uf3AmbientContrib", ambient);

    glDepthMask(GL_FALSE);
    RenderQuad();
    glDepthMask(GL_TRUE);
}

void GLRenderer::RenderFramebuffers()
{
    SetShaderProgram(m_diagnosticProg);
    SetTexturesForFullScreenPass();
    m_diagnosticProg->SetTexture("u_Colortex", m_colorTexture);

    glDepthMask(GL_FALSE);
    RenderQuad();
    glDepthMask(GL_TRUE);
}

void GLRenderer::RenderPostProcessEffects()
{
    SetShaderProgram(m_postProg);
    SetTexturesForFullScreenPass();
    m_postProg->SetTexture("u_Posttex", m_postTexture);

    glDepthMask(GL_FALSE);
    RenderQuad();
    glDepthMask(GL_TRUE);
}

void GLRenderer::RenderQuad()
{
    DrawGeometry(&m_QuadGeometry);
}

void GLRenderer::SetFramebufferActive(GLType_uint fbID)
{
    assert(fbID < m_FBO.size());
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO[fbID]);
}

void GLRenderer::SetShaderProgram(GLProgram* currentlyUsedProgram)
{ 
    m_currentProgram = currentlyUsedProgram; 
    m_currentProgram->SetActive();
}

void GLRenderer::SetTexturesForFullScreenPass()
{
    m_currentProgram->SetTexture("u_Depthtex", m_depthTexture);
    m_currentProgram->SetTexture("u_Normaltex", m_normalTexture);
    m_currentProgram->SetTexture("u_Positiontex", m_positionTexture);
    m_currentProgram->SetTexture("u_RandomNormaltex", m_randomNormalTexture);
    m_currentProgram->SetTexture("u_RandomScalartex", m_randomScalarTexture);
}