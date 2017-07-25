#include "Scene.h"

using namespace G3D::units;


void Scene::onSimulation(GameTime deltaTime) {
    m_time += deltaTime;
    for (int i = 0; i < m_entityArray.size(); ++i) {
        m_entityArray[i]->onSimulation(m_time, deltaTime);
    }
}


/** Returns a table mapping scene names to filenames */
static Table<std::string, std::string>& filenameTable() {
    static Table<std::string, std::string> filenameTable;

    if (filenameTable.size() == 0) {
        // Create a table mapping scene names to filenames
        Array<std::string> filenameArray;

        FileSystem::ListSettings settings;
        settings.files = true;
        settings.directories = false;
        settings.includeParentPath = true;
        settings.recursive = true;

        FileSystem::list("*.scn.any", filenameArray, settings);

        logLazyPrintf("Found scenes:\n");
        for (int i = 0; i < filenameArray.size(); ++i) {
            Any a;
            std::string msg;
            try {
                a.load(filenameArray[i]);
                
                const std::string& name = a["name"].string();
                alwaysAssertM(! filenameTable.containsKey(name),
                              "Duplicate scene names in " + filenameArray[i] + " and " +
                              filenameTable[name]);
                
                msg = format("  \"%s\" (%s)\n", name.c_str(), filenameArray[i].c_str());
                filenameTable.set(name, filenameArray[i]);
            } catch (const ParseError& e) {
                msg = format("  <Parse error at %s:%d(%d): %s>\n", e.filename.c_str(), e.line, e.character, e.message.c_str());
            } catch (...) {
                msg = format("  <Error while loading %s>\n", filenameArray[i].c_str());
            }

            if (! msg.empty()) {
                logLazyPrintf("%s", msg.c_str());
                debugPrintf("%s", msg.c_str());
            }
        }
        logPrintf("");
    }

    return filenameTable;
}


Array<std::string> Scene::sceneNames() {
    Array<std::string> a;
    filenameTable().getKeys(a);
    a.sort();
    return a;
}


Scene::Ref Scene::create(const std::string& scene, GCamera& camera) {
    if (scene == "") {
        return NULL;
    }

    Scene::Ref s = new Scene();

    const std::string* f = filenameTable().getPointer(scene);
    if (f == NULL) {
        throw "No scene with name '" + scene + "' found in (" + 
            stringJoin(filenameTable().getKeys(), ", ") + ")";
    }
    const std::string& filename = *f;

    Any any;
    any.load(filename);
    s->m_sourceAny = any;

    // Load the lighting
    s->m_lighting = Lighting::create(any.get("lighting", Lighting::Specification()));

    // Load the models
    Any models = any["models"];
    typedef ReferenceCountedPointer<ReferenceCountedObject> ModelRef;
    Table< std::string, ModelRef > modelTable;
    for (Any::AnyTable::Iterator it = models.table().begin(); it.isValid(); ++it) {
        ModelRef m;
        Any v = it->value;
        if (v.nameBeginsWith("ArticulatedModel")) {
            m = ArticulatedModel::create(v);
            m.downcast<ArticulatedModel>()->name = it->key;
        } else if (v.nameBeginsWith("MD2Model")) {
            m = MD2Model::create(v);
        } else if (v.nameBeginsWith("MD3Model")) {
            m = MD3Model::create(v);
        } else {
            debugAssertM(false, "Unrecognized model type: " + v.name());
        }

        modelTable.set(it->key, m);        
    }

    // Instance the models
    Any entities = any["entities"];
    for (Table<std::string, Any>::Iterator it = entities.table().begin(); it.isValid(); ++it) {
        const std::string& name = it->key;

        AnyTableReader propertyTable(it->value);
        if (it->value.nameEquals("Entity")) {
            s->m_entityArray.append(Entity::create(name, propertyTable, modelTable));
        } 
        
        propertyTable.verifyDone();
    }

    // Load the camera
    camera = any["camera"];


    // Use the environment map as a skybox if there isn't one already, and vice versa
    if (any.containsKey("skyBox")) {
        Any sky = any["skyBox"];
		sky.verifyType(Any::TABLE);
		sky.verifyName("");
        s->m_skyBoxConstant = sky.get("constant", 1.0f);
        if (sky.containsKey("texture")) {
            s->m_skyBoxTexture = Texture::create(sky["texture"]);
        }
    } else {
        s->m_skyBoxTexture = s->m_lighting->environmentMapTexture;
        s->m_skyBoxConstant = s->m_lighting->environmentMapConstant;
    }

    // Default to using the skybox as an environment map if none is specified.
    if (s->m_skyBoxTexture.notNull() && s->m_lighting->environmentMapTexture.isNull()) {
        s->m_lighting->environmentMapTexture  = s->m_skyBoxTexture;
        s->m_lighting->environmentMapConstant = s->m_skyBoxConstant;
    }

    if ((s->m_skyBoxTexture->dimension() != Texture::DIM_CUBE_MAP) && 
        (s->m_skyBoxTexture->dimension() != Texture::DIM_CUBE_MAP_NPOT)) {
        throw std::string("skyBox texture must be a cube map.");
    }
    
    if ((s->m_lighting->environmentMapTexture->dimension() != Texture::DIM_CUBE_MAP) && 
        (s->m_lighting->environmentMapTexture->dimension() != Texture::DIM_CUBE_MAP_NPOT)) {
        throw std::string("environmentMap texture must be a cube map.");
    }

    // Set the initial positions
    for (int e = 0; e < s->m_entityArray.size(); ++e) {
        s->m_entityArray[e]->onSimulation(0, 0);
    }

    return s;
}


void Scene::onPose(Array<Surface::Ref>& surfaceArray) {
    for (int e = 0; e < m_entityArray.size(); ++e) {
        m_entityArray[e]->onPose(surfaceArray);
    }
}


Entity::Ref Scene::intersectBounds(const Ray& ray, float& distance, const Array<Entity::Ref>& exclude) {
    Entity::Ref closest = NULL;
    
    for (int e = 0; e < m_entityArray.size(); ++e) {
        const Entity::Ref& entity = m_entityArray[e];
        if (! exclude.contains(entity) && entity->intersectBounds(ray, distance)) {
            closest = entity;
        }
    }

    return closest;
}


Entity::Ref Scene::intersect(const Ray& ray, float& distance, const Array<Entity::Ref>& exclude) {
    Entity::Ref closest = NULL;
    
    for (int e = 0; e < m_entityArray.size(); ++e) {
        const Entity::Ref& entity = m_entityArray[e];
        if (! exclude.contains(entity) && entity->intersect(ray, distance)) {
            closest = entity;
        }
    }

    return closest;
}


Any Scene::toAny() const {
    Any a = m_sourceAny;

    // Overwrite the entity table
    Any entityTable(Any::TABLE);
    for (int i = 0; i < m_entityArray.size(); ++i) {
        const Entity::Ref& entity = m_entityArray[i];
        entityTable[entity->name()] = entity->toAny();
    }

    return a;
}
