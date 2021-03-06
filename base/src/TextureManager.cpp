#include "TextureManager.h"
#include <algorithm>
#include <string>
#include <sstream>
#include "GLRenderer.h"
#include "Utility.h"
#include "gl/glew.h"

extern "C"
{
    #include "SOIL/SOIL.h"
    #include "SOIL/image_DXT.h"
    #include "SOIL/image_helper.h"
}

std::weak_ptr<TextureManager> TextureManager::singleton;

namespace
{
    enum
    {
        SOIL_FLAG_SRGB_TEXTURE = 1024
    };

    bool IsNonPowerOfTwoTextureDimsSupported()
    {
        int32_t numExts;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (int32_t i = 0; i < numExts; ++i)
        {
            if (strstr(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)), "GL_ARB_texture_non_power_of_two"))
            {
                return true;
            }
        }
        
        return false;
    }

    bool IsS3TCSupported()
    {
        int32_t numExts;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (int32_t i = 0; i < numExts; ++i)
        {
            if (strstr(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)), "GL_EXT_texture_compression_s3tc"))
            {
                return true;
            }
        }

        return false;
    }
}

TextureManager::TextureManager()
{}

TextureManager::~TextureManager()
{
    for (auto& eachElement : m_textureNameToObjectMap)
        glDeleteTextures(1, &eachElement.first);

    m_textureNameToObjectMap.clear();
}

std::shared_ptr<TextureManager> TextureManager::GetSingleton()
{
    try
    {
        return std::shared_ptr<TextureManager>(singleton);
    }
    catch (std::bad_weak_ptr&)
    {
        try
        {
            TextureManager* pTextureManager = new TextureManager;
            std::shared_ptr<TextureManager> newTextureManager = std::shared_ptr<TextureManager>(pTextureManager);
            singleton = newTextureManager;
            return newTextureManager;
        }
        catch (std::bad_alloc&)
        {
            assert(false); // Out of memory!
        }
    }
}

GLType_uint TextureManager::Acquire(const std::string& textureName)
{
    uint32_t textureNameHash = Utility::HashCString(textureName.c_str());
    GLType_uint acquiredTextureObject = 0;

    if (textureName.length())
    {
        auto mapItr = m_textureNameToObjectMap.find(textureNameHash);
        if (mapItr != m_textureNameToObjectMap.end())
        {
            std::pair<GLType_uint, uint32_t>& textureObject = mapItr->second;
            ++textureObject.second;

            acquiredTextureObject = textureObject.first;
        }
        else
        {
            std::pair<GLType_uint, uint32_t> newTextureObject;
            newTextureObject.first = LoadImageAndCreateTexture(textureName, 0, 0, SOIL_FLAG_TEXTURE_REPEATS | SOIL_FLAG_INVERT_Y);
            newTextureObject.second = 1;
            m_textureNameToObjectMap[textureNameHash] = newTextureObject;

            acquiredTextureObject = newTextureObject.first;
        }
    }

    return acquiredTextureObject;
}

void TextureManager::Release(GLType_uint textureObject)
{
    bool found = false;

    auto iterator = m_textureNameToObjectMap.begin();
    for (; iterator != m_textureNameToObjectMap.end(); ++iterator)
    {
        if (iterator->second.first == textureObject)
        {
            found = true;
            break;
        }
    }
    
    if (found)
    {
        --iterator->second.second;
        if (iterator->second.second == 0)
        {
            glDeleteTextures(1, &iterator->second.first);
            m_textureNameToObjectMap.erase(iterator->first);
        }
    }
    else
    {
        assert(false);  // Trying to Release a texture that was not Acquired?
    }
}

// SOIL code copy-pasted an modified as required.
GLType_uint TextureManager::LoadImageAndCreateTexture(const std::string& textureName, int32_t forceChannels, uint32_t reuseTextureName, uint32_t flags)
{
    unsigned char* img;
    int width, height, channels;

    /*	try to load the image	*/
    img = SOIL_load_image(textureName.c_str(), &width, &height, &channels, forceChannels);
    /*	channels holds the original number of channels, which may have been forced	*/
    if ((forceChannels >= 1) && (forceChannels <= 4))
    {
        channels = forceChannels;
    }
    if (NULL == img)
    {
        /*	image loading failed	*/
        Utility::LogMessage("Texture file: ");  
        Utility::LogMessage(textureName.c_str());
        Utility::LogMessageAndEndLine(" doesn't exist.");
        return 0;
    }

    GLType_uint opengl_texture_type = GL_TEXTURE_2D;
    GLType_uint opengl_texture_target = GL_TEXTURE_2D;

    /*	variables	*/
    unsigned int tex_id;
    unsigned int internalFormat = 0, pixelFormat = 0;
    int max_supported_size;
    /*	If the user wants to use the texture rectangle I kill a few flags	*/
    if (flags & SOIL_FLAG_TEXTURE_RECTANGLE)
    {
        /*	only allow this if the user in _NOT_ trying to do a cubemap!	*/
        if (opengl_texture_type == GL_TEXTURE_2D)
        {
            /*	clean out the flags that cannot be used with texture rectangles	*/
            flags &= ~(
                SOIL_FLAG_POWER_OF_TWO | SOIL_FLAG_MIPMAPS |
                SOIL_FLAG_TEXTURE_REPEATS
                );
            /*	and change my target	*/
            opengl_texture_target = GL_TEXTURE_RECTANGLE;
            opengl_texture_type = GL_TEXTURE_RECTANGLE;
        }
        else
        {
            /*	not allowed for any other uses (yes, I'm looking at you, cubemaps!)	*/
            flags &= ~SOIL_FLAG_TEXTURE_RECTANGLE;
        }
    }

    /*	does the user want me to invert the image?	*/
    if (flags & SOIL_FLAG_INVERT_Y)
    {
        int i, j;
        for (j = 0; j * 2 < height; ++j)
        {
            int index1 = j * width * channels;
            int index2 = (height - 1 - j) * width * channels;
            for (i = width * channels; i > 0; --i)
            {
                unsigned char temp = img[index1];
                img[index1] = img[index2];
                img[index2] = temp;
                ++index1;
                ++index2;
            }
        }
    }
    /*	does the user want me to scale the colors into the NTSC safe RGB range?	*/
    if (flags & SOIL_FLAG_NTSC_SAFE_RGB)
    {
        scale_image_RGB_to_NTSC_safe(img, width, height, channels);
    }
    /*	does the user want me to convert from straight to pre-multiplied alpha?
    (and do we even _have_ alpha?)	*/
    if (flags & SOIL_FLAG_MULTIPLY_ALPHA)
    {
        int i;
        switch (channels)
        {
        case 2:
            for (i = 0; i < 2 * width*height; i += 2)
            {
                img[i] = (img[i] * img[i + 1] + 128) >> 8;
            }
            break;
        case 4:
            for (i = 0; i < 4 * width*height; i += 4)
            {
                img[i + 0] = (img[i + 0] * img[i + 3] + 128) >> 8;
                img[i + 1] = (img[i + 1] * img[i + 3] + 128) >> 8;
                img[i + 2] = (img[i + 2] * img[i + 3] + 128) >> 8;
            }
            break;
        default:
            /*	no other number of channels contains alpha data	*/
            break;
        }
    }
    /*	if the user can't support NPOT textures, make sure we force the POT option	*/
    if (!IsNonPowerOfTwoTextureDimsSupported() && !(flags & SOIL_FLAG_TEXTURE_RECTANGLE))
    {
        /*	add in the POT flag */
        flags |= SOIL_FLAG_POWER_OF_TWO;
    }
    /*	how large of a texture can this OpenGL implementation handle?	*/
    /*	texture_check_size_enum will be GL_MAX_TEXTURE_SIZE or SOIL_MAX_CUBE_MAP_TEXTURE_SIZE	*/
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_supported_size);

    /*	do I need to make it a power of 2?	*/
    if (
        (flags & SOIL_FLAG_POWER_OF_TWO) ||	/*	user asked for it	*/
        (flags & SOIL_FLAG_MIPMAPS) ||		/*	need it for the MIP-maps	*/
        (width > max_supported_size) ||		/*	it's too big, (make sure it's	*/
        (height > max_supported_size))		/*	2^n for later down-sampling)	*/
    {
        int new_width = 1;
        int new_height = 1;
        while (new_width < width)
        {
            new_width *= 2;
        }
        while (new_height < height)
        {
            new_height *= 2;
        }
        /*	still?	*/
        if ((new_width != width) || (new_height != height))
        {
            /*	yep, resize	*/
            unsigned char *resampled = (unsigned char*)malloc(channels*new_width*new_height);
            up_scale_image(
                img, width, height, channels,
                resampled, new_width, new_height);
            /*	OJO	this is for debug only!	*/
            /*
            SOIL_save_image( "\\showme.bmp", SOIL_SAVE_TYPE_BMP,
            new_width, new_height, channels,
            resampled );
            */
            /*	nuke the old guy, then point it at the new guy	*/
            SOIL_free_image_data(img);
            img = resampled;
            width = new_width;
            height = new_height;
        }
    }
    /*	now, if it is too large...	*/
    if ((width > max_supported_size) || (height > max_supported_size))
    {
        /*	I've already made it a power of two, so simply use the MIPmapping
        code to reduce its size to the allowable maximum.	*/
        unsigned char *resampled;
        int reduce_block_x = 1, reduce_block_y = 1;
        int new_width, new_height;
        if (width > max_supported_size)
        {
            reduce_block_x = width / max_supported_size;
        }
        if (height > max_supported_size)
        {
            reduce_block_y = height / max_supported_size;
        }
        new_width = width / reduce_block_x;
        new_height = height / reduce_block_y;
        resampled = (unsigned char*)malloc(channels*new_width*new_height);
        /*	perform the actual reduction	*/
        mipmap_image(img, width, height, channels,
            resampled, reduce_block_x, reduce_block_y);
        /*	nuke the old guy, then point it at the new guy	*/
        SOIL_free_image_data(img);
        img = resampled;
        width = new_width;
        height = new_height;
    }
    /*	does the user want us to use YCoCg color space?	*/
    if (flags & SOIL_FLAG_CoCg_Y)
    {
        /*	this will only work with RGB and RGBA images */
        convert_RGB_to_YCoCg(img, width, height, channels);
        /*
        save_image_as_DDS( "CoCg_Y.dds", width, height, channels, img );
        */
    }
    /*	create the OpenGL texture ID handle
    (note: allowing a forced texture ID lets me reload a texture)	*/
    tex_id = reuseTextureName;
    if (tex_id == 0)
    {
        glCreateTextures(opengl_texture_type, 1, &tex_id);
    }

    /* Note: sometimes glGenTextures fails (usually no OpenGL context)	*/
    if (tex_id)
    {
        bool isS3TCSupported = IsS3TCSupported();
        /*	and what type am I using as the internal texture format?	*/
        switch (channels)
        {
        case 1:
            pixelFormat = GL_RED;
            internalFormat = (isS3TCSupported && (flags & SOIL_FLAG_COMPRESS_TO_DXT)) ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_R8;
            break;
        case 2:
            pixelFormat = GL_RG;
            internalFormat = (isS3TCSupported && (flags & SOIL_FLAG_COMPRESS_TO_DXT)) ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_RG8;
            break;
        case 3:
            pixelFormat = GL_RGB;
            internalFormat = (isS3TCSupported && (flags & SOIL_FLAG_COMPRESS_TO_DXT)) ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_RGB8;
            break;
        case 4:
            pixelFormat = GL_RGBA;
            internalFormat = (isS3TCSupported && (flags & SOIL_FLAG_COMPRESS_TO_DXT)) ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_RGBA8;
            break;
        }
        if (flags & SOIL_FLAG_SRGB_TEXTURE)
        {
            if (internalFormat == GL_RGB8)
                internalFormat = GL_SRGB8;
            else if (internalFormat == GL_RGBA8)
                internalFormat = GL_SRGB8_ALPHA8;
            else if (internalFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
                internalFormat = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
            else if (internalFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
                internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
        }

        uint32_t numMipLevels = 0; // Mip levels ABOVE base level of 0.
        if (flags & SOIL_FLAG_MIPMAPS)
        {
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            while ((mipWidth > 1) || (mipHeight > 1))
            {
                mipWidth = std::max<uint32_t>(mipWidth >> 1, 1);
                mipHeight = std::max<uint32_t>(mipHeight >> 1, 1);

                ++numMipLevels;
            }
        }

        // Create the immutable storage for the texture.
        glTextureStorage2D(tex_id, numMipLevels + 1, internalFormat, width, height);
        
        /*	does the user want me to, and can I, save as DXT?	*/
        if (flags & SOIL_FLAG_COMPRESS_TO_DXT)
        {
            if (isS3TCSupported)
            {
                /*	I can use DXT, whether I compress it or OpenGL does	*/
                /*	user wants me to do the DXT conversion!	*/
                int DDS_size;
                unsigned char *DDS_data = NULL;
                if ((channels & 1) == 1)
                {
                    /*	RGB, use DXT1	*/
                    DDS_data = convert_image_to_DXT1(img, width, height, channels, &DDS_size);
                }
                else
                {
                    /*	RGBA, use DXT5	*/
                    DDS_data = convert_image_to_DXT5(img, width, height, channels, &DDS_size);
                }
                if (DDS_data)
                {
                    glCompressedTextureSubImage2D(tex_id, 0, 0, 0, width, height, internalFormat, DDS_size, DDS_data);
                    SOIL_free_image_data(DDS_data);
                    /*	printf( "Internal DXT compressor\n" );	*/
                }
                else
                {
                    /*	my compression failed, try the OpenGL driver's version	*/
                    glTextureSubImage2D(tex_id, 0, 0, 0, width, height, pixelFormat, GL_UNSIGNED_BYTE, img);
                    /*	printf( "OpenGL DXT compressor\n" );	*/
                }
            }
        }
        else
        {
            /*  upload the main image without compression */
            glTextureSubImage2D(tex_id, 0, 0, 0, width, height, pixelFormat, GL_UNSIGNED_BYTE, img);
            /*printf( "OpenGL DXT compressor\n" );	*/
        }
        /*	are any MIPmaps desired?	*/
        if (numMipLevels > 0)
        {
            uint32_t MIPwidth = (width + 1) / 2, MIPheight = (height + 1) / 2;
            unsigned char *resampled = (unsigned char*)malloc(channels*MIPwidth*MIPheight);
           
            for (uint32_t MIPlevel = 1; MIPlevel <= numMipLevels; ++MIPlevel)
            {
                /*	do this MIPmap level	*/
                mipmap_image(
                    img, width, height, channels,
                    resampled,
                    (1 << MIPlevel), (1 << MIPlevel));
                /*  upload the MIPmaps	*/
                if (isS3TCSupported)
                {
                    /*	user wants me to do the DXT conversion!	*/
                    int DDS_size;
                    unsigned char *DDS_data = NULL;
                    if ((channels & 1) == 1)
                    {
                        /*	RGB, use DXT1	*/
                        DDS_data = convert_image_to_DXT1(
                            resampled, MIPwidth, MIPheight, channels, &DDS_size);
                    }
                    else
                    {
                        /*	RGBA, use DXT5	*/
                        DDS_data = convert_image_to_DXT5(
                            resampled, MIPwidth, MIPheight, channels, &DDS_size);
                    }
                    if (DDS_data)
                    {
                        glCompressedTextureSubImage2D(tex_id, MIPlevel, 0, 0, MIPwidth, MIPheight, internalFormat, DDS_size, DDS_data);
                        SOIL_free_image_data(DDS_data);
                    }
                    else
                    {
                        /*	my compression failed, try the OpenGL driver's version	*/
                        glTextureSubImage2D(tex_id, MIPlevel, 0, 0, MIPwidth, MIPheight, pixelFormat, GL_UNSIGNED_BYTE, resampled);
                    }
                }
                else
                {
                    /*	user want OpenGL to do all the work!	*/
                    glTextureSubImage2D(tex_id, MIPlevel, 0, 0, MIPwidth, MIPheight, pixelFormat, GL_UNSIGNED_BYTE, resampled);
                }

                MIPwidth = (width + 1) / 2;
                MIPheight = (height + 1) / 2;
            }
            free(resampled);
            /*	instruct OpenGL to use the MIPmaps	*/
            glTextureParameteri(tex_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(tex_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(tex_id, GL_TEXTURE_MAX_LEVEL, numMipLevels);
        }
        else
        {
            /*	instruct OpenGL _NOT_ to use the MIPmaps	*/
            glTextureParameteri(tex_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(tex_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(tex_id, GL_TEXTURE_MAX_LEVEL, 0);
        }
        /*	does the user want clamping, or wrapping?	*/
        if (flags & SOIL_FLAG_TEXTURE_REPEATS)
        {
            glTextureParameteri(tex_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTextureParameteri(tex_id, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (opengl_texture_type == GL_TEXTURE_CUBE_MAP)
            {
                /*	SOIL_TEXTURE_WRAP_R is invalid if cubemaps aren't supported	*/
                glTextureParameteri(tex_id, GL_TEXTURE_WRAP_R, GL_REPEAT);
            }
        }
        else
        {
            /*	unsigned int clamp_mode = SOIL_CLAMP_TO_EDGE;	*/
            unsigned int clamp_mode = GL_CLAMP_TO_EDGE;
            glTextureParameteri(tex_id, GL_TEXTURE_WRAP_S, clamp_mode);
            glTextureParameteri(tex_id, GL_TEXTURE_WRAP_T, clamp_mode);
            if (opengl_texture_type == GL_TEXTURE_CUBE_MAP)
            {
                /*	SOIL_TEXTURE_WRAP_R is invalid if cubemaps aren't supported	*/
                glTextureParameteri(tex_id, GL_TEXTURE_WRAP_R, clamp_mode);
            }
        }
    }
    else
    {
        // Failed to generate an OpenGL texture name; missing OpenGL context?
        assert(false);
    }
    SOIL_free_image_data(img);
    return tex_id;
}