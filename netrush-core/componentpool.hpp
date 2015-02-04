#pragma once

#include <boost/container/flat_map.hpp>

#include "id.hpp"
#include "workqueue.hpp"

namespace netrush {
namespace core {

    /** Pool of component objects of type T.
        Requires T to be at least movable and copy-constructible.

        Components can be moved around in memory so the user code should never rely on
        the component's address.
        Components are accessed using their ids.
        Most operations are thread-safe but will be executed only on the next update() call.

        The update() function should always be called by only one thread at a time and is not
        thread-safe.
        The classic way to use it is to have one thread or thread pool calling regularly the
        update() function and having other threads calling the other operations.
    */
    template< class T >
    class ComponentPool
    {
        using ComponentMap = boost::container::flat_map< UUID, T >;
        ComponentMap m_components;
        core::WorkQueue<> m_work_queue;
    public:

        /** Constructor, immediately reserving memory for specified object count.
            @param reserved_count   Number of objects for which memory will be reserved immediately.
        */
        explicit ComponentPool( size_t reserved_count );

        /** Reserve memory for the specified count of objects.
            Does nothing if the current memory capacity is enough to contain the specified
            count of objects.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param count    Number of objects for which memory should be reserved.
        */
        void reserve( size_t count );

        /** Reserve memory for the specified count of objects in addition to the current count.
            Does nothing if the current memory capacity is enough to contain the specified
            count of objects plus the current count.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param count    Number of objects for which memory should be reserved in addition to the current count.
        */
        void reserve_extra( size_t count );

        /** Create a new component object associated with the provided id.
            Does nothing if the provided id is already stored in this pool.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param id   Id of the object to create.
            @param args Initialization arguments passed to the constructor of the object.
        */
        template< class... Args >
        void create( const UUID& id, Args... args );

        /** Create a new component object associated with the provided id using the default constructor.
            Does nothing if the provided id is already stored in this pool.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param id   Id of the object to create.
            @param args Initialization arguments passed to the constructor of the object.
        */
        void create( const UUID& id );

        /** Destroy the component associated with the specified id.
            Does nothing if the id is not registered here.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param id   Id of the component to destroy.
        */
        void destroy( const UUID& id );

        /** Destroy any component which match the provided predicate.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param predicate    Predicate taking arguments ( const UUID&, const T& ) and returning true if the object must be destroyed, false otherwise.
        */
        template< class Predicate >
        void destroy_if( Predicate predicate );

        /** Destroy all components contained in this pool.
            @remark This is a thread-safe call but will be applied only on the next update() call.
        */
        void clear();

        /** Apply the provided function to each object and id.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param func     Callable taking arguments ( const UUID&, T& ) that will be applied to all components.
        */
        template< class Func >
        void for_each( Func&& func );

        /** Schedule the provided work to be applied to the component associated with the provided id.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param id       Id of the component to work on.
            @param work     Callable taking arguments ( T& ) that will work with the component object.
        */
        template< class Func >
        void schedule( const UUID& id, Func work );

        /** Schedule work to be applied for each component object matching the provided predicate.
            @remark This is a thread-safe call but will be applied only on the next update() call.

            @param predicate    Predicate taking arguments ( const UUID&, const T& ) and returning true if the component must be worked on.
            @param work         Callable taking arguments ( T& ) that will work with the component matching the predicate.
        */
        template< class Func, class Predicate >
        void schedule_if( Predicate predicate, Func work );

        /** Execute all operations since the last update() call.
            All operations accumulated by calling the other modifying functions between the last call and
            this one will be executed now.

            @remark Call this function regularly to keep the pool up to date.
                    But make sure that only one thread at a time is calling this function.

            @warning This is NOT a thread-safe function and it should never be called by several threads at the same time.
        */
        void update();

        auto begin()    const ->    decltype(m_components.begin())  { return m_components.begin(); }
        auto begin()          ->    decltype(m_components.begin())  { return m_components.begin(); }
        auto end()      const ->    decltype(m_components.end())    { return m_components.end(); }
        auto end()            ->    decltype(m_components.end())    { return m_components.end(); }

    };

    template< class T >
    ComponentPool<T>::ComponentPool( size_t reserved_count )
    {
        m_components.reserve( reserved_count );
    }

    template< class T >
    void ComponentPool<T>::reserve( size_t count )
    {
        m_work_queue.push( [=]{
            m_components.reserve( count );
        });
    }

    template< class T >
    void ComponentPool<T>::reserve_extra( size_t count )
    {
        m_work_queue.push( [=] {
            m_components.reserve( count + m_components.size() );
        } );
    }


    template< class T >
    template< class... Args >
    void ComponentPool<T>::create( const UUID& id, Args... args )
    {
        m_work_queue.push( [=]{
            m_components.emplace( id, T(args...) );
        });
    }

    template< class T >
    void ComponentPool<T>::create( const UUID& id )
    {
        m_work_queue.push( [=] {
            m_components[id];
        } );
    }

    template< class T >
    void ComponentPool<T>::destroy( const UUID& id )
    {
        m_work_queue.push( [=]{
            m_components.erase( id );
        });
    }

    template< class T >
    template< class Predicate >
    void ComponentPool<T>::destroy_if( Predicate predicate )
    {
        m_work_queue.push( [=]{
            auto remove_it = std::remove_if( begin( m_components ), end( m_components ) , [&]( const std::pair<const UUID, T>& slot ) {
                const auto& id = slot.first;
                const auto& object = slot.second;
                return predicate( id, object );
            });
            m_components.erase( remove_it, end( m_components ) );
        });
    }

    template< class T >
    void ComponentPool<T>::clear()
    {
        m_work_queue.push( [&]{
            m_components.clear();
        });
    }

    template< class T >
    template< class Func >
    void ComponentPool<T>::for_each( Func&& func )
    {
        m_work_queue.push( [=]{
            for( auto&& slot : m_components )
                func( slot.first, slot.second );
        });
    }

    template< class T >
    template< class Func >
    void ComponentPool<T>::schedule( const UUID& id, Func work )
    {
        m_work_queue.push( [=]{
            auto find_it = m_components.find( id );
            if( find_it != m_components.end() )
                work( find_it->second );
        });
    }

    template< class T >
    template< class Func, class Predicate >
    void ComponentPool<T>::schedule_if( Predicate predicate, Func work )
    {
        m_work_queue.push( [=]{
            for( auto&& slot : m_components )
            {
                if( predicate( slot.first, slot.second ) )
                    work( slot.second );
            }
        });
    }

    template< class T >
    void ComponentPool<T>::update()
    {
        m_work_queue.execute();
    }


}}

