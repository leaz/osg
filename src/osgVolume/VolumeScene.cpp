/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2009 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include <osgVolume/VolumeScene>
#include <osg/Geometry>
#include <osg/PrimitiveSet>
#include <osg/Geode>
#include <osg/ValueObject>
#include <osg/io_utils>
#include <osgDB/ReadFile>
#include <OpenThreads/ScopedLock>

using namespace osgVolume;

class RTTCameraCullCallback : public osg::NodeCallback
{
    public:

        RTTCameraCullCallback(VolumeScene* vs):
            _volumeScene(vs) {}

        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv)
        {
            osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(nv);

            _volumeScene->osg::Group::traverse(*nv);

            node->setUserValue("CalculatedNearPlane",double(cv->getCalculatedNearPlane()));
            node->setUserValue("CalculatedFarPlane",double(cv->getCalculatedFarPlane()));
        }

    protected:

        virtual ~RTTCameraCullCallback() {}

        osgVolume::VolumeScene* _volumeScene;

};


VolumeScene::ViewData::ViewData()
{
}


VolumeScene::VolumeScene()
{
}

VolumeScene::VolumeScene(const VolumeScene& vs, const osg::CopyOp& copyop):
    osg::Group(vs,copyop)
{
}


VolumeScene::~VolumeScene()
{
}

void VolumeScene::tileVisited(osg::NodeVisitor* nv, osgVolume::VolumeTile* tile)
{
    osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(nv);
    if (cv)
    {
        osg::ref_ptr<ViewData> viewData;

        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_viewDataMapMutex);
            viewData = _viewDataMap[cv];
        }

        if (viewData.valid())
        {
            osg::ref_ptr<TileData> tileData = new TileData;
            tileData->nodePath = cv->getNodePath();
            tileData->projectionMatrix = cv->getProjectionMatrix();
            tileData->modelviewMatrix = cv->getModelViewMatrix();
            viewData->_tiles.push_back(tileData.get());
        }
    }
}


void VolumeScene::traverse(osg::NodeVisitor& nv)
{
    osgUtil::CullVisitor* cv = dynamic_cast<osgUtil::CullVisitor*>(&nv);
    if (cv)
    {
        osg::ref_ptr<ViewData> viewData;
        bool initializeViewData = false;
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_viewDataMapMutex);
            if (!_viewDataMap[cv])
            {
                _viewDataMap[cv] = new ViewData;
                initializeViewData = true;
            }

            viewData = _viewDataMap[cv];
        }

        if (initializeViewData)
        {
            OSG_NOTICE<<"Creating ViewData"<<std::endl;

            unsigned textureWidth = 512;
            unsigned textureHeight = 512;

            osg::Viewport* viewport = cv->getCurrentRenderStage()->getViewport();
            if (viewport)
            {
                textureWidth = viewport->width();
                textureHeight = viewport->height();
            }

            // set up depth texture
            viewData->_depthTexture = new osg::Texture2D;

            viewData->_depthTexture->setTextureSize(textureWidth, textureHeight);
            viewData->_depthTexture->setInternalFormat(GL_DEPTH_COMPONENT);
            viewData->_depthTexture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::LINEAR);
            viewData->_depthTexture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::LINEAR);

            viewData->_depthTexture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::CLAMP_TO_BORDER);
            viewData->_depthTexture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::CLAMP_TO_BORDER);
            viewData->_depthTexture->setBorderColor(osg::Vec4(1.0f,1.0f,1.0f,1.0f));

            // set up color texture
            viewData->_colorTexture = new osg::Texture2D;

            viewData->_colorTexture->setTextureSize(textureWidth, textureHeight);
            viewData->_colorTexture->setInternalFormat(GL_RGBA);
            viewData->_colorTexture->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::LINEAR);
            viewData->_colorTexture->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::LINEAR);

            viewData->_colorTexture->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::CLAMP_TO_EDGE);
            viewData->_colorTexture->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::CLAMP_TO_EDGE);


            // set up the RTT Camera to capture the main scene to a color and depth texture that can be used in post processing
            viewData->_rttCamera = new osg::Camera;
            viewData->_rttCamera->attach(osg::Camera::DEPTH_BUFFER, viewData->_depthTexture.get());
            viewData->_rttCamera->attach(osg::Camera::COLOR_BUFFER, viewData->_colorTexture.get());
            viewData->_rttCamera->setCullCallback(new RTTCameraCullCallback(this));
            viewData->_rttCamera->setViewport(0,0,textureWidth,textureHeight);

            // clear the depth and colour bufferson each clear.
            viewData->_rttCamera->setClearMask(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

            // set the camera to render before the main camera.
            viewData->_rttCamera->setRenderOrder(osg::Camera::PRE_RENDER);

            // tell the camera to use OpenGL frame buffer object where supported.
            viewData->_rttCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);


            viewData->_rttCamera->setReferenceFrame(osg::Transform::RELATIVE_RF);
            viewData->_rttCamera->setProjectionMatrix(osg::Matrixd::identity());
            viewData->_rttCamera->setViewMatrix(osg::Matrixd::identity());

            // create mesh for rendering the RTT textures onto the screen
            osg::ref_ptr<osg::Geode> geode = new osg::Geode;
            geode->setCullingActive(false);

            viewData->_backdropSubgraph = geode;
            //geode->addDrawable(osg::createTexturedQuadGeometry(osg::Vec3(-1.0f,-1.0f,-1.0f),osg::Vec3(2.0f,0.0f,-1.0f),osg::Vec3(0.0f,2.0f,-1.0f)));

            viewData->_geometry = new osg::Geometry;
            geode->addDrawable(viewData->_geometry.get());

            viewData->_geometry->setUseDisplayList(false);
            viewData->_geometry->setUseVertexBufferObjects(false);

            viewData->_vertices = new osg::Vec3Array(4);
            viewData->_geometry->setVertexArray(viewData->_vertices.get());

            osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array(1);
            (*colors)[0].set(1.0f,1.0f,1.0f,1.0f);
            viewData->_geometry->setColorArray(colors.get(), osg::Array::BIND_OVERALL);

            osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array(4);
            (*texcoords)[0].set(0.0f,1.0f);
            (*texcoords)[1].set(0.0f,0.0f);
            (*texcoords)[2].set(1.0f,1.0f);
            (*texcoords)[3].set(1.0f,0.0f);
            viewData->_geometry->setTexCoordArray(0, texcoords.get(), osg::Array::BIND_PER_VERTEX);

            viewData->_geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_STRIP,0,4));


            osg::ref_ptr<osg::StateSet> stateset = viewData->_geometry->getOrCreateStateSet();

            stateset->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
            stateset->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
            stateset->setRenderBinDetails(10,"DepthSortedBin");

            osg::ref_ptr<osg::Program> program = new osg::Program;
            stateset->setAttribute(program);

            // get vertex shaders from source
            osg::ref_ptr<osg::Shader> vertexShader = osgDB::readRefShaderFile(osg::Shader::VERTEX, "shaders/volume_color_depth.vert");
            if (vertexShader.valid())
            {
                program->addShader(vertexShader.get());
            }
#if 0
            else
            {
                #include "Shaders/volume_color_depth_vert.cpp"
                program->addShader(new osg::Shader(osg::Shader::VERTEX, volume_color_depth_vert));
            }
#endif
            // get fragment shaders from source
            osg::ref_ptr<osg::Shader> fragmentShader = osgDB::readRefShaderFile(osg::Shader::FRAGMENT, "shaders/volume_color_depth.frag");
            if (fragmentShader.valid())
            {
                program->addShader(fragmentShader.get());
            }
#if 0
            else
            {
                #include "Shaders/volume_color_depth_frag.cpp"
                program->addShader(new osg::Shader(osg::Shader::FRAGMENT, volume_color_depth_frag));
            }
#endif
            viewData->_stateset = new osg::StateSet;
            viewData->_stateset->addUniform(new osg::Uniform("colorTexture",0));
            viewData->_stateset->addUniform(new osg::Uniform("depthTexture",1));

            viewData->_stateset->setTextureAttributeAndModes(0, viewData->_colorTexture.get(), osg::StateAttribute::ON);
            viewData->_stateset->setTextureAttributeAndModes(1, viewData->_depthTexture.get(), osg::StateAttribute::ON);

            viewData->_viewportSizeUniform = new osg::Uniform("viewportSize",osg::Vec2(1280.0,1024.0));
            viewData->_stateset->addUniform(viewData->_viewportSizeUniform.get());

            geode->setStateSet(viewData->_stateset.get());

        }
        else
        {
            // OSG_NOTICE<<"Reusing ViewData"<<std::endl;

        }

        osg::Matrix projectionMatrix = *(cv->getProjectionMatrix());
        osg::Matrix modelviewMatrix = *(cv->getModelViewMatrix());


        // new frame so need to clear last frames log of VolumeTiles
        viewData->_tiles.clear();

        osg::Viewport* viewport = cv->getCurrentRenderStage()->getViewport();
        if (viewport)
        {
            viewData->_viewportSizeUniform->set(osg::Vec2(viewport->width(),viewport->height()));

            if (viewport->width() != viewData->_colorTexture->getTextureWidth() ||
                viewport->height() != viewData->_colorTexture->getTextureHeight())
            {
                OSG_NOTICE<<"Need to change texture size to "<<viewport->width()<<", "<< viewport->height()<<std::endl;
                viewData->_colorTexture->setTextureSize(viewport->width(), viewport->height());
                viewData->_colorTexture->dirtyTextureObject();
                viewData->_depthTexture->setTextureSize(viewport->width(), viewport->height());
                viewData->_depthTexture->dirtyTextureObject();
                viewData->_rttCamera->setViewport(0, 0, viewport->width(), viewport->height());
                if (viewData->_rttCamera->getRenderingCache())
                {
                    viewData->_rttCamera->getRenderingCache()->releaseGLObjects(0);
                }
            }
        }

        cv->setUserValue("VolumeSceneTraversal",std::string("RenderToTexture"));

        osg::Camera* parentCamera = cv->getCurrentCamera();
        viewData->_rttCamera->setClearColor(parentCamera->getClearColor());

        //OSG_NOTICE<<"Ready to traverse RTT Camera"<<std::endl;
        //OSG_NOTICE<<"   RTT Camera ProjectionMatrix Before "<<viewData->_rttCamera->getProjectionMatrix()<<std::endl;
        viewData->_rttCamera->accept(nv);

        //OSG_NOTICE<<"   RTT Camera ProjectionMatrix After "<<viewData->_rttCamera->getProjectionMatrix()<<std::endl;
        //OSG_NOTICE<<"   cv ProjectionMatrix After "<<*(cv->getProjectionMatrix())<<std::endl;

        //OSG_NOTICE<<"  after RTT near ="<<cv->getCalculatedNearPlane()<<std::endl;
        //OSG_NOTICE<<"  after RTT far ="<<cv->getCalculatedFarPlane()<<std::endl;

        //OSG_NOTICE<<"tileVisited()"<<viewData->_tiles.size()<<std::endl;


        double calculatedNearPlane = DBL_MAX;
        double calculatedFarPlane = -DBL_MAX;
        if (viewData->_rttCamera->getUserValue("CalculatedNearPlane",calculatedNearPlane) &&
            viewData->_rttCamera->getUserValue("CalculatedFarPlane",calculatedFarPlane))
        {
            calculatedNearPlane *= 0.5;
            calculatedFarPlane *= 2.0;

            //OSG_NOTICE<<"Got from RTTCamera CalculatedNearPlane="<<calculatedNearPlane<<std::endl;
            //OSG_NOTICE<<"Got from RTTCamera CalculatedFarPlane="<<calculatedFarPlane<<std::endl;
            if (calculatedNearPlane < cv->getCalculatedNearPlane()) cv->setCalculatedNearPlane(calculatedNearPlane);
            if (calculatedFarPlane > cv->getCalculatedFarPlane()) cv->setCalculatedFarPlane(calculatedFarPlane);
        }

        if (calculatedFarPlane>calculatedNearPlane)
        {
            cv->clampProjectionMatrix(projectionMatrix, calculatedNearPlane, calculatedFarPlane);
        }

        osg::Matrix inv_projectionModelViewMatrix;
        inv_projectionModelViewMatrix.invert(modelviewMatrix*projectionMatrix);

        double depth = 1.0;
        osg::Vec3d v00 = osg::Vec3d(-1.0,-1.0,depth)*inv_projectionModelViewMatrix;
        osg::Vec3d v01 = osg::Vec3d(-1.0,1.0,depth)*inv_projectionModelViewMatrix;
        osg::Vec3d v10 = osg::Vec3d(1.0,-1.0,depth)*inv_projectionModelViewMatrix;
        osg::Vec3d v11 = osg::Vec3d(1.0,1.0,depth)*inv_projectionModelViewMatrix;

        // OSG_NOTICE<<"v00= "<<v00<<std::endl;
        // OSG_NOTICE<<"v01= "<<v01<<std::endl;
        // OSG_NOTICE<<"v10= "<<v10<<std::endl;
        // OSG_NOTICE<<"v11= "<<v11<<std::endl;

        (*(viewData->_vertices))[0] = v01;
        (*(viewData->_vertices))[1] = v00;
        (*(viewData->_vertices))[2] = v11;
        (*(viewData->_vertices))[3] = v10;
        viewData->_geometry->dirtyBound();

        //OSG_NOTICE<<"  new after RTT near ="<<cv->getCalculatedNearPlane()<<std::endl;
        //OSG_NOTICE<<"  new after RTT far ="<<cv->getCalculatedFarPlane()<<std::endl;

        viewData->_backdropSubgraph->accept(*cv);

        osg::NodePath nodePathPriorToTraversingSubgraph = cv->getNodePath();
        cv->setUserValue("VolumeSceneTraversal",std::string("Post"));

        // for each tile that needs post rendering we need to add it into current RenderStage.
        Tiles& tiles = viewData->_tiles;
        for(Tiles::iterator itr = tiles.begin();
            itr != tiles.end();
            ++itr)
        {
            TileData* tileData = itr->get();
            unsigned int numStateSetPushed = 0;

            // OSG_NOTICE<<"VolumeTile to add "<<tileData->projectionMatrix.get()<<", "<<tileData->modelviewMatrix.get()<<std::endl;


            osg::NodePath& nodePath = tileData->nodePath;

            cv->getNodePath() = nodePath;
            cv->pushProjectionMatrix(tileData->projectionMatrix.get());
            cv->pushModelViewMatrix(tileData->modelviewMatrix.get(), osg::Transform::ABSOLUTE_RF_INHERIT_VIEWPOINT);


            cv->pushStateSet(viewData->_stateset.get());
            ++numStateSetPushed;


            osg::NodePath::iterator np_itr = nodePath.begin();

            // skip over all nodes above VolumeScene as this will have already been traverse by CullVisitor
            while(np_itr!=nodePath.end() && (*np_itr)!=viewData->_rttCamera.get()) { ++np_itr; }
            if (np_itr!=nodePath.end()) ++np_itr;

            // push the stateset on the nodes between this VolumeScene and the VolumeTile
            for(osg::NodePath::iterator ss_itr = np_itr;
                ss_itr != nodePath.end();
                ++ss_itr)
            {
                if ((*ss_itr)->getStateSet())
                {
                    numStateSetPushed++;
                    cv->pushStateSet((*ss_itr)->getStateSet());
                    // OSG_NOTICE<<"  pushing StateSet"<<std::endl;
                }
            }
            cv->traverse(*(tileData->nodePath.back()));

            // pop the StateSet's
            for(unsigned int i=0; i<numStateSetPushed; ++i)
            {
                cv->popStateSet();
                // OSG_NOTICE<<"  popping StateSet"<<std::endl;
            }

            cv->popModelViewMatrix();
            cv->popProjectionMatrix();
        }

        cv->getNodePath() = nodePathPriorToTraversingSubgraph;
    }
    else
    {
        Group::traverse(nv);
    }
}
