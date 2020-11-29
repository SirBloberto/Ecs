#pragma once

#include <bitset>
#include <queue>
#include <vector>
#include <unordered_map>

namespace Ecs {
    using Type = std::vector<unsigned int>;
    using TypeHash = std::bitset<64>;

    using Chunk = std::vector<void*>;

    template<typename Component>
    struct ComponentArray {
        Component* data;

        ComponentArray(void* pointer);

        Component& operator[](unsigned int index);
    };

    struct Archetype {
        Type type;
        std::vector<Chunk> components;
        std::vector<unsigned int> entities;
        unsigned int count;

        template<typename Component>
        ComponentArray<Component> GetChunk();
    };

    struct Record {
        Archetype* archetype;
        unsigned int index;
    };

    class World {
        std::unordered_map<unsigned int, Record> entities;
        std::unordered_map<TypeHash, Archetype*> archetypes;

        std::queue<unsigned int> removed;
        unsigned int entityIndex = 0;

        bool shouldStop;

        unsigned int CreateEntity();
        Archetype* GetArchetype(TypeHash hash);
        Record& GetRecord(unsigned int id);
    public:
        ~World();

        bool Run();
        void Finish();

        std::unordered_map<TypeHash, Archetype*> GetArchetypes();

        friend class Entity;
    };

    class Entity {
        World* world;
        unsigned int id;

    public:
        Entity(World& world);
        ~Entity();

        template<typename Component>
        Entity& Add();

        template<typename Component, typename... Arguments>
        Entity& Set(Arguments&&... arguments);

        template<typename Component>
        Entity& Remove();

        template<typename Component>
        bool Has();

        template<typename Component>
        Component& Get();
    };

    template<typename... Components, typename... Arguments, typename Function>
    void System(World& world, Function function, Arguments&&... arguments);

        static unsigned int componentId = 0;

    template<typename Component>
    unsigned int ComponentId() {
        static unsigned int id = componentId++;
        return id;
    }

    TypeHash Hash(Type type) {
        unsigned int hash = 0;
        for(unsigned int component : type)
            hash |= (1ull << component);
        return TypeHash(hash);
    }

    Entity::Entity(World& world)
        : world(&world), id(world.CreateEntity()) {}

    Entity::~Entity(){
        Record& record = world->GetRecord(id);
        Archetype* archetype = record.archetype;
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            archetype->components[index][record.index] = archetype->components[index][archetype->count - 1];
            archetype->components[index].pop_back();
        }
        archetype->entities[record.index] = archetype->entities[archetype->count - 1];
        archetype->entities.pop_back();
        world->GetRecord(archetype->entities[record.index]).index = record.index;
        archetype->count--;
        world->entities.erase(id);
        world->removed.push(id);
    }

    template<typename Component>
    Entity& Entity::Add(){
        Record& record = world->GetRecord(id);
        Archetype* archetype = record.archetype;
        std::queue<void*> data;
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            data.push(archetype->components[index][record.index]);
            archetype->components[index][record.index] = archetype->components[index][archetype->count - 1];
            archetype->components[index].pop_back();
        }
        archetype->entities[record.index] = archetype->entities[archetype->count - 1];
        archetype->entities.pop_back();
        world->GetRecord(archetype->entities[record.index]).index = record.index;
        archetype->count--;
        unsigned int componentId = ComponentId<Component>();
        TypeHash hash = Hash(archetype->type);
        Type type = { componentId };
        hash |= Hash(type);
        archetype = world->GetArchetype(hash);
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            if(archetype->type[index] == componentId)
                archetype->components[index].push_back(new Component());
            else {
                archetype->components[index].push_back(data.front());
                data.pop();
            }
        }
        record.archetype = archetype;
        record.index = record.archetype->count++;
        archetype->entities.push_back(id);
        return *this;
    }

    template<typename Component, typename... Arguments>
    Entity& Entity::Set(Arguments&&... arguments){
        if(!Has<Component>())
            Add<Component>();
        Record& record = world->GetRecord(id);
        Archetype* archetype = record.archetype;
        unsigned int componentId = ComponentId<Component>();
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            if(archetype->type[index] == componentId)
                archetype->components[index][record.index] = new Component{arguments...};
        }
        return *this;
    }

    template<typename Component>
    Entity& Entity::Remove(){
        Record& record = world->GetRecord(id);
        Archetype* archetype = record.archetype;
        unsigned int componentId = ComponentId<Component>();
        std::queue<void*> data;
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            if(archetype->type[index] == componentId) {
                (*(Component*)archetype->components[index][record.index]).~Component();
            } else 
                data.push(archetype->components[index][record.index]);
            archetype->components[index][record.index] = archetype->components[index][archetype->count - 1];
            archetype->components[index].pop_back();
        }
        archetype->entities[record.index] = archetype->entities[archetype->count - 1];
        archetype->entities.pop_back();
        world->GetRecord(archetype->entities[record.index]).index = record.index;
        archetype->count--;
        TypeHash hash = Hash(archetype->type);
        Type type = { componentId };
        hash ^= Hash(type);
        archetype = world->GetArchetype(hash);
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            archetype->components[index].push_back(data.front());
            data.pop();
        }
        record.archetype = archetype;
        record.index = record.archetype->count++;
        return *this;
    }

    template<typename Component>
    bool Entity::Has(){
        Archetype* archetype = world->GetRecord(id).archetype;
        unsigned int componentId = ComponentId<Component>();
        for(unsigned int index = 0; index < archetype->type.size(); index++) {
            if(archetype->type[index] == componentId)
                return true;
        }
        return false;
    }

    template<typename Component>
    Component& Entity::Get(){
        Record& record = world->GetRecord(id);
        Archetype* archetype = record.archetype;
        unsigned int componentId = ComponentId<Component>();
        for(unsigned int index = 0; index < archetype-> type.size(); index++){
            if(archetype->type[index] == componentId)
                return *(Component*)archetype->components[index][record.index];
        }
        throw std::invalid_argument("Does not exist");
    }

    template<typename... Components, typename... Arguments, typename Function>
    void System(World& world, Function function, Arguments&&... arguments){
        TypeHash hash = Hash({ComponentId<Components>()...});
        for(auto& pair : world.GetArchetypes()){
            std::bitset<64> temporary = hash;
            if((temporary &= pair.first) == hash){
                for(unsigned int id : pair.second->entities)
                    function(pair.second->GetChunk<Components>()[id]..., arguments...);
            }
        }
    }

    template<typename Component>
    ComponentArray<Component>::ComponentArray(void* pointer)
        : data((Component*)pointer) {}

    template<typename Component>
    Component& ComponentArray<Component>::operator[](unsigned int index){
        return data[index];
    }

    template<typename Component>
    ComponentArray<Component> Archetype::GetChunk(){
        unsigned int componentId = ComponentId<Component>();
        for(unsigned int index = 0; index < type.size(); index++){
            if(type[index] == componentId)
                return ComponentArray<Component>(*components[index].data());
        }
        throw std::invalid_argument("Does not exist");
    }
    
    unsigned int World::CreateEntity() {
        unsigned int id;
        if(removed.empty())
            id = entityIndex++;
        else {
            id = removed.front();
            removed.pop();
        }
        Record record;
        record.archetype = GetArchetype(TypeHash());
        record.archetype->entities.push_back(id);
        record.index = record.archetype->count;
        record.archetype->count++;
        entities.insert({id, record});
        return id;
    }
    Record& World::GetRecord(unsigned int id) {
        std::unordered_map<unsigned int, Record>::iterator iterator = entities.find(id);
        if(iterator != entities.end())
            return iterator->second;
        throw std::invalid_argument("Entity id does not exist");
    }

    Archetype* World::GetArchetype(TypeHash hash) {
        std::unordered_map<TypeHash, Archetype*>::iterator iterator = archetypes.find(hash);
        if(iterator != archetypes.end())
            return iterator->second;
        Type type;
        for(unsigned int index = 0; index < hash.size(); index++) {
            if(hash[index] == 1)
                type.push_back(index);
        }
        Archetype* archetype = new Archetype();
        archetype->type = type;
        archetype->components.resize(type.size());
        archetypes.insert({hash, archetype});
        return archetype;
    }

    World::~World(){
        entities.clear();
        for(unsigned int index = 0; index < archetypes.size(); index++)
            delete archetypes[index];
        archetypes.clear();
        for(unsigned int index = 0; index < removed.size(); index++)
            removed.pop();
        shouldStop = true;
    }

    bool World::Run(){
        return !shouldStop;
    }

    void World::Finish(){
        shouldStop = true;
    }

    std::unordered_map<TypeHash, Archetype*> World::GetArchetypes(){
        return archetypes;
    }
}