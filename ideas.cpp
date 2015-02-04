
namespace mcs // Meta Component System
{
	class ID
	{};

	enum class ComponentRequestResult
	{
		ACKDONE
	,	NACK_ID_ALREADY_EXISTS
	,	NACK_ID_NOT_FOUND
	};

	template< class T >
	class ComponentStore
	{
	public:

		void reserve( size_t );

		template< class... Args >
		ComponentRequestResult create( ID<T> new_id, Args... init_args );

		ComponentRequestResult destroy( ID<T> id );

	private:
		// CONCEPT-BASED
	};


	class ComponentCluster
	{
	public:

		template< class... ComponentType >
		void add( ComnponentStore<ComponentType>... stores )
		{

		}




	private:

	};

}


void usage()
{




}

Architecture strategies
========================

Component store: store component instances of the same type, managed in specific way.
Cluster: gather component stores that must be updated together.
Updater: object that have to be called with components as arguments each time 
components are updated.

1. Component stores allowed
---------------------------

 A. Cluster with unique store type.
    - All component types have to be managed the same way.
    + make() can take arguments.
    + batch visitor function.

 B. Cluster composed of heterogenous compoenent stores.
    - cannot use arguments in make()
    + component stores can have different management strategies.
    + uniform update.

  CHOICE: Provide different cluster types for each solution, use a component store
          concept-based polymorphic type for B.

2. Concurrency
--------------
 
 A. No thread-safe interface for cluster
 B. Cluster garantee that each component store is manipulated safely
     (queue-based or mutex based)
 C. Both cluster and it's component stores all have thread-safe interface.

  CHOICE: ?

3. Runtime errors
-----------------

 A. Return responses to requests by value.
 B. Return responses to requests by exception.
 C. Return responses to requests by expected.

   CHOICE: ?

4. Compile-time errors
----------------------

 A. No types check.
    + dynamic composition of types as necessary
    - no checks at compile time 
    
 B. Provide a fixed set of types for a cluster.
    + compile-time check of allowed types
    + potential optimizations (arrays instead of vectors)
    - compilation time 
    - all types must be defined upfront on compilation


   CHOICE: ?

5. Components identifiers
-------------------------

 A. User is forced to use an identifier matching our concept.
 B. User can use any identifier as long as it's copyable and equality-comparable.

   CHOICE: Both. 

6. Id generation
-----------------

 CHOICE: User provide ids. 

7. Entity object.
-----------------

 Wrap ids and maybe pointers to components.

8. Component types dependencies
-------------------------------

 A. Component store types have a way to specify component types dependencies.
 B. Component cluster have a way to specify a type dependencies table.


9. Cluster call for components update
-------------------------------------

 A. call operator leads to call operator of all components, with same arguments
 B. visitor call, either for one type (and compatibles), or with all types 

 CHOICE: Both. If component type do not have call operator, do nothing for this type.
 If it don't have the right arguments, do nothing either. If it's an empty call operator,
 always call it.
 'A' with arguments can be done only for component cluster that know their component store types. 

10. Component types update order
 A. No order guaranteed.
 B. Order index optionally provided by component stores.

11. Update strategy
-------------------

 - in-place
 - double buffer? 

12. Component stores types.
---------------------------

 1. ordered, or not? by id? by other comparison?
 2. value semantic or entity semantic?
 3. activation/construction, deactivation/destruction
 or activation/activate, deactivation/deactivate  

 CHOICE: provide component stores for most useful cases

13. Access optimizations
------------------------

 A. None.
 B. Ids can be converted to pointers.

 CHOICE: meh.

14. Updaters
------------
  Called after update.
  A. function< void( ComponentType& ) >
  B. concept-based polymorphic type
  C. shared_ptr<ComponentUpdater>

15. update parallelism
-----------------------

16. update signature 
---------------------

