/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2015 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgEarthAnnotation/PlaceNode>
#include <osgEarthAnnotation/AnnotationUtils>
#include <osgEarthAnnotation/AnnotationRegistry>
#include <osgEarthFeatures/BuildTextFilter>
#include <osgEarthFeatures/LabelSource>
#include <osgEarth/Utils>
#include <osgEarth/Registry>
#include <osgEarth/ShaderGenerator>
#include <osgEarth/Decluttering>

#include <osg/Depth>
#include <osgText/Text>

#define LC "[PlaceNode] "

using namespace osgEarth;
using namespace osgEarth::Annotation;
using namespace osgEarth::Features;
using namespace osgEarth::Symbology;


PlaceNode::PlaceNode(MapNode*           mapNode,
                     const GeoPoint&    position,
                     osg::Image*        image,
                     const std::string& text,
                     const Style&       style ) :

GeoPositionNode( mapNode, position ),
_image   ( image ),
_text    ( text ),
_style   ( style ),
_geode   ( 0L )
{
    init();
}

PlaceNode::PlaceNode(MapNode*           mapNode,
                     const GeoPoint&    position,
                     const std::string& text,
                     const Style&       style ) :

GeoPositionNode( mapNode, position ),
_text    ( text ),
_style   ( style ),
_geode   ( 0L )
{
    init();
}

PlaceNode::PlaceNode(MapNode*              mapNode,
                     const GeoPoint&       position,
                     const Style&          style,
                     const osgDB::Options* dbOptions ) :
GeoPositionNode ( mapNode, position ),
_style    ( style ),
_dbOptions( dbOptions )
{
    init();
}

void
PlaceNode::init()
{
    Decluttering::setEnabled( this->getOrCreateStateSet(), true );

    osgEarth::clearChildren( getPositionAttitudeTransform() );

    _geode = new osg::Geode();

    // ensure that (0,0,0) is the bounding sphere control/center point.
    // useful for things like horizon culling.
    _geode->setComputeBoundingSphereCallback(new ControlPointCallback());

    osg::Drawable* text = 0L;

    // If there's no explicit text, look to the text symbol for content.
    if ( _text.empty() && _style.has<TextSymbol>() )
    {
        _text = _style.get<TextSymbol>()->content()->eval();
    }

    osg::ref_ptr<const InstanceSymbol> instance = _style.get<InstanceSymbol>();

    // backwards compability, support for deprecated MarkerSymbol
    if ( !instance.valid() && _style.has<MarkerSymbol>() )
    {
        instance = _style.get<MarkerSymbol>()->convertToInstanceSymbol();
    }

    const IconSymbol* icon = instance->asIcon();

    if ( !_image.valid() )
    {
        URI imageURI;

        if ( icon )
        {
            if ( icon->url().isSet() )
            {
                imageURI = icon->url()->evalURI();
            }
            else if (icon->getImage())
            {
                _image = icon->getImage();
            }
        }

        if ( !imageURI.empty() )
        {
            _image = imageURI.getImage( _dbOptions.get() );
        }
    }

    osg::BoundingBox imageBox(0,0,0,0,0,0);

    // found an image; now format it:
    if ( _image.get() )
    {
        // Scale the icon if necessary
        double scale = 1.0;
        if ( icon && icon->scale().isSet() )
        {
            scale = icon->scale()->eval();
        }

        double s = scale * _image->s();
        double t = scale * _image->t();

        // this offset anchors the image at the bottom
        osg::Vec2s offset;
        if ( !icon || !icon->alignment().isSet() )
        {	
            // default to bottom center
            offset.set(0.0, t / 2.0);
        }
        else
        {	// default to bottom center
            switch (icon->alignment().value())
            {
            case IconSymbol::ALIGN_LEFT_TOP:
                offset.set((s / 2.0), -(t / 2.0));
                break;
            case IconSymbol::ALIGN_LEFT_CENTER:
                offset.set((s / 2.0), 0.0);
                break;
            case IconSymbol::ALIGN_LEFT_BOTTOM:
                offset.set((s / 2.0), (t / 2.0));
                break;
            case IconSymbol::ALIGN_CENTER_TOP:
                offset.set(0.0, -(t / 2.0));
                break;
            case IconSymbol::ALIGN_CENTER_CENTER:
                offset.set(0.0, 0.0);
                break;
            case IconSymbol::ALIGN_CENTER_BOTTOM:
            default:
                offset.set(0.0, (t / 2.0));
                break;
            case IconSymbol::ALIGN_RIGHT_TOP:
                offset.set(-(s / 2.0), -(t / 2.0));
                break;
            case IconSymbol::ALIGN_RIGHT_CENTER:
                offset.set(-(s / 2.0), 0.0);
                break;
            case IconSymbol::ALIGN_RIGHT_BOTTOM:
                offset.set(-(s / 2.0), (t / 2.0));
                break;
            }
        }

        // Apply a rotation to the marker if requested:
        double heading = 0.0;
        if ( icon && icon->heading().isSet() )
        {
            heading = osg::DegreesToRadians( icon->heading()->eval() );
        }

        //We must actually rotate the geometry itself and not use a MatrixTransform b/c the 
        //decluttering doesn't respect Transforms above the drawable.
        osg::Geometry* imageGeom = AnnotationUtils::createImageGeometry( _image.get(), offset, 0, heading, scale );
        if ( imageGeom )
        {
            _geode->addDrawable( imageGeom );
            imageBox = osgEarth::Utils::getBoundingBox( imageGeom );
        }    
    }

    if ( _image.valid() )
    {
        TextSymbol* textSymbol = _style.getOrCreate<TextSymbol>();
        if ( !textSymbol->alignment().isSet() )
            textSymbol->alignment() = textSymbol->ALIGN_LEFT_CENTER;
    }
    
    text = AnnotationUtils::createTextDrawable(
            _text,
            _style.get<TextSymbol>(),
            imageBox );

    if ( text )
        _geode->addDrawable( text );
    
    osg::StateSet* stateSet = _geode->getOrCreateStateSet();
    stateSet->setAttributeAndModes( new osg::Depth(osg::Depth::ALWAYS, 0, 1, false), 1 );

    getPositionAttitudeTransform()->addChild( _geode );

    // for clamping and occlusion culling    
    //OE_WARN << LC << "PlaceNode::applyStyle: " << _style.getConfig().toJSON(true) << std::endl;
    applyStyle( _style );

    setLightingIfNotSet( false );
    
    // generate shaders:
    Registry::shaderGenerator().run(
        this,
        "osgEarth.PlaceNode",
        Registry::stateSetCache() );

    setPriority(getPriority());

    if ( _dynamic )
        setDynamic( _dynamic );

    updateLayoutData();
}

void
PlaceNode::setPriority(float value)
{
    GeoPositionNode::setPriority(value);
    updateLayoutData();
}

void
PlaceNode::updateLayoutData()
{
    osg::ref_ptr<LayoutData> data = new LayoutData();
    data->_priority = getPriority();
    const TextSymbol* ts = getStyle().get<TextSymbol>();
    if (ts) data->_pixelOffset = ts->pixelOffset().get();

    // re-apply annotation drawable-level stuff as neccesary.
    for (unsigned i = 0; i<_geode->getNumDrawables(); ++i)
    {
        _geode->getDrawable(i)->setUserData(data.get());
    }
}

void
PlaceNode::setText( const std::string& text )
{
    if ( !_dynamic )
    {
        OE_WARN << LC << "Illegal state: cannot change a LabelNode that is not dynamic" << std::endl;
        return;
    }

    _text = text;

    for(unsigned i=0; i<_geode->getNumDrawables(); ++i)
    {
        osgText::Text* d = dynamic_cast<osgText::Text*>( _geode->getDrawable(i) );
        if ( d )
        {
			TextSymbol* symbol =  _style.getOrCreate<TextSymbol>();
			osgText::String::Encoding text_encoding = osgText::String::ENCODING_UNDEFINED;
			if ( symbol && symbol->encoding().isSet() )
			{
				text_encoding = AnnotationUtils::convertTextSymbolEncoding(symbol->encoding().value());
			}

            d->setText( text, text_encoding );
            break;
        }
    }
}


void
PlaceNode::setStyle(const Style& style)
{
    // changing the style requires a complete rebuild.
    _style = style;
    init();
}


void
PlaceNode::setIconImage(osg::Image* image)
{
    // changing the icon requires a complete rebuild.
    _image = image;
    init();
}


void
PlaceNode::setDynamic( bool value )
{
    GeoPositionNode::setDynamic( value );
    
    for(unsigned i=0; i<_geode->getNumDrawables(); ++i)
    {
        _geode->getDrawable(i)->setDataVariance( 
            value ? osg::Object::DYNAMIC : osg::Object::STATIC );
    }
}



//-------------------------------------------------------------------

OSGEARTH_REGISTER_ANNOTATION( place, osgEarth::Annotation::PlaceNode );


PlaceNode::PlaceNode(MapNode*              mapNode,
                     const Config&         conf,
                     const osgDB::Options* dbOptions) :
GeoPositionNode ( mapNode, conf ),
_dbOptions( dbOptions )
{
    conf.getObjIfSet( "style",  _style );
    conf.getIfSet   ( "text",   _text );

    optional<URI> imageURI;
    conf.getIfSet( "icon", imageURI );
    if ( imageURI.isSet() )
    {
        _image = imageURI->getImage();
        if ( _image.valid() )
            _image->setFileName( imageURI->base() );
    }

    init();
}

Config
PlaceNode::getConfig() const
{
    Config conf( "place" );
    conf.add   ( "text",   _text );
    conf.addObj( "style",  _style );
    if ( _image.valid() ) {
        if ( !_image->getFileName().empty() )
            conf.add( "icon", _image->getFileName() );
        else if ( !_image->getName().empty() )
            conf.add( "icon", _image->getName() );
    }

    return conf;
}
