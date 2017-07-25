#include "Entity.h"

Entity::Entity
(const std::string& name,
 AnyTableReader&    propertyTable,
 const ModelTable&  modelTable) : 
    GEntity(name, propertyTable, modelTable) {
}


Entity::Ref Entity::create
(const std::string& name,
 AnyTableReader&    propertyTable,
 const ModelTable&  modelTable) {

    return new Entity(name, propertyTable, modelTable);
}
