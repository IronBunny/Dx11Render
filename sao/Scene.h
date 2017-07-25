#ifndef Scene_h
#define Scene_h

#include <G3D/G3DAll.h>
#include "Entity.h"


/** \brief Sample scene graph.

    Includes loading from a text file, and a GUI component for detecting
    and selecting scenes.

    G3D does not contain a Scene class in the API because it is a
    very application-specific role. This is a sample of how you 
    might begin to structure one to get you started.
*/
class Scene : public ReferenceCountedObject {
protected:
    
    /** The Any from which this scene was constructed. */
    Any                         m_sourceAny;

    /** Current time */
    GameTime                    m_time;
    Lighting::Ref               m_lighting;
    Texture::Ref                m_skyBoxTexture;
    float                       m_skyBoxConstant;
    Array<Entity::Ref>          m_entityArray;

    Scene() : 
        m_time(0), 
        m_skyBoxTexture(Texture::whiteCube()), 
        m_skyBoxConstant(1.0f) {}

public:

    typedef ReferenceCountedPointer<Scene> Ref;

    static Scene::Ref create(const std::string& sceneName, GCamera& camera);

    /** Creates an Any representing this scene by updating the one
     from which it was loaded with the current Entity positions.  This
     will overwrite any <code>\#include</code> entries that appeared in
     the original source Any.

     You can obtain the original filename as a.source().filename().
    */
    Any toAny() const;

    virtual void onPose(Array<Surface::Ref>& surfaceArray);

    virtual void onSimulation(GameTime deltaTime);

    Lighting::Ref lighting() const {
        return m_lighting;
    }

    GameTime time() const {
        return m_time;
    }

    void getEntityNames(Array<std::string>& names) const {
        for (int e = 0; e < m_entityArray.size(); ++e) {
            names.append(m_entityArray[e]->name());
        }
    }

    /** Get an entity by name */
    const Entity::Ref entity(const std::string& name) const {
        for (int e = 0; e < m_entityArray.size(); ++e) {
            if (name == m_entityArray[e]->name()) {
                return m_entityArray[e];
            }
        }
        return NULL;
    }

    Texture::Ref skyBoxTexture() const {
        return m_skyBoxTexture;
    }

    float skyBoxConstant() const {
        return m_skyBoxConstant;
    }

    /** Enumerate the names of all available scenes. */
    static Array<std::string> sceneNames();

    /** Returns the Entity whose conservative bounds are first
        intersected by \a ray, excluding Entity%s in \a exclude.  
        Useful for mouse selection and coarse hit-scan collision detection.  
        Returns NULL if none are intersected.
        
        Note that this may not return the closest Entity if another's bounds
        project in front of it.

        \param ray World space ray

        \param distance Maximum distance at which to allow selection
        (e.g., finf()).  On return, this is the distance to the
        object.

        \param exclude Entities to ignore when searching for occlusions.  This is
        convenient to use when avoiding self-collisions, for example.
     */  
    Entity::Ref intersectBounds(const Ray& ray, float& distance, const Array<Entity::Ref>& exclude = Array<Entity::Ref>());

    /** Performs very precise (usually, ray-triangle) intersection, and is much slower
        than intersectBounds.  */
    Entity::Ref intersect(const Ray& ray, float& distance, const Array<Entity::Ref>& exclude = Array<Entity::Ref>());
};

#endif
