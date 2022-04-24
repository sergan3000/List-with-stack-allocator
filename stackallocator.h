#include <iostream>
#include <memory>
#include <vector>
#include <list>

template<size_t N>
class alignas(max_align_t) StackStorage {
private:
    char pool[N];
    void* current = nullptr;
public:
    StackStorage(): current(&pool) {

    }

    StackStorage& operator=(const StackStorage& stack_storage) = default;

    void* findSpace(size_t n, size_t alignment) {
        size_t space = N;
        void* alignedStart = std::align(alignment, n, current, space);
        char* new_current = static_cast<char*>(alignedStart) + n;
        current = static_cast<void*>(new_current);

        return alignedStart;
    }

    ~StackStorage() = default;
};

template<typename T, size_t poolSize>
class StackAllocator {
private:
    StackStorage<poolSize>* pool;
public:
    using value_type = T;

    template<typename U>
    struct rebind {
        using other = StackAllocator<U, poolSize>;
    };

    template<size_t N>
    StackAllocator(StackStorage<N>& storage): pool(&storage) {

    }

    StackStorage<poolSize>* getPool() {
        return pool;
    }

    template<typename F, size_t size>
    StackAllocator(StackAllocator<F, size> alloc): pool(alloc.getPool()){
    }

    template<typename F, size_t size>
    StackAllocator& operator=(StackAllocator<F, size> alloc) {
        pool = alloc.getPool();
    }

    T* allocate(size_t n) {
        return reinterpret_cast<T*>(pool->findSpace(n * sizeof(T), alignof(T)));
    }

    void deallocate(T*, size_t) {

    }

    ~StackAllocator() = default;

};

template<typename T, typename Allocator = std::allocator<T>>
class List {
private:
    size_t sz = 0;

    struct BaseNode {
        BaseNode* prev = this;
        BaseNode* next = this;

        BaseNode() = default;

        ~BaseNode() = default;

    };

    struct Node : BaseNode {
        T value;

        Node() = default;

        // CE since c++20. Why?
        Node(BaseNode* prev, BaseNode* next, const T& value) : BaseNode{prev, next}, value(value) {

        }

        Node(BaseNode* prev, BaseNode* next) : BaseNode{prev, next} {

        }

        ~Node() = default;
    };


    BaseNode* fake = nullptr;
    Allocator allocator;
    using AllocTraits = std::allocator_traits<Allocator>;
    using NodeAlloc = typename AllocTraits::template rebind_alloc<Node>;
    NodeAlloc node_allocator = allocator;
    using NodeAllocTraits = typename AllocTraits::template rebind_traits<Node>;

    void allocate_amount_nodes(size_t amount) {
        sz = amount;
        fake = NodeAllocTraits::allocate(node_allocator, 1);
        fake->prev = fake;
        fake->next = fake;
        BaseNode* last = (fake);
        for (size_t i = 0; i < amount; i++) {
            try {
                Node* new_node = NodeAllocTraits::allocate(node_allocator, 1);
                new_node->next = fake;
                new_node->prev = last;
                last->next = new_node;
                fake->prev = new_node;
                last = new_node;
            } catch(...) {
                while(last != fake) {
                    last = last->prev;
                    NodeAllocTraits::deallocate(node_allocator, static_cast<Node*>(last->next), 1);
                }
                throw;
            }
        }
    }

    void return_to_normal(Node* it) {
        Node* it3 = static_cast<Node*>(fake->next);
        Node* reverse_it = static_cast<Node*>(fake->prev);
        while (it3 != it) {
            Node* it2 = static_cast<Node*>(it3->next);
            NodeAllocTraits::destroy(node_allocator, it3);
            NodeAllocTraits::deallocate(node_allocator, it3, 1);
            it3 = it2;
        }
        while (reverse_it != it) {
            Node* reverse_it2 = static_cast<Node*>(reverse_it->prev);
            NodeAllocTraits::deallocate(node_allocator, reverse_it, 1);
            reverse_it = reverse_it2;
        }
        NodeAllocTraits::deallocate(node_allocator, reverse_it, 1);
        throw;
    }

    void construct_default_list() {
        Node* it = static_cast<Node*>(fake->next);
        while (it != fake) {
            try {
                NodeAllocTraits::construct(node_allocator, it, it->prev, it->next);
                it = static_cast<Node*>(it->next);
            } catch (...) {
                return_to_normal(it);
                throw;
            }

        }
    }

    void construct_value_list(const T& value) {
        Node* it = static_cast<Node*>(fake->next);
        while (it != fake) {
            try {
                NodeAllocTraits::construct(node_allocator, it, it->prev, it->next, value);
                it = static_cast<Node*>(it->next);
            } catch(...) {
                return_to_normal(it);
                throw;
            }
        }
    }

    void swap_without_allocator(List& list) {
        std::swap(sz, list.sz);
        std::swap(fake, list.fake);
    }

    void swap(List& list) {
        swap_without_allocator(list);
        if (AllocTraits::propagate_on_container_swap::value) {
            std::swap(allocator, list.allocator);
        }
    }

public:

    List() {
        allocate_amount_nodes(0);
    }

    ~List() {
        if (sz == 0) return;
        Node* it = static_cast<Node*>(fake->next);
        while (it != fake->prev) {
            Node* it2 = static_cast<Node*>(it->next);
            NodeAllocTraits::destroy(node_allocator, it);
            NodeAllocTraits::deallocate(node_allocator, it, 1);
            it = it2;
        }
        NodeAllocTraits::destroy(node_allocator, it);
        NodeAllocTraits::deallocate(node_allocator, it, 1);
    }

    List(size_t amount) {
        allocate_amount_nodes(amount);
        construct_default_list();
    }

    List(size_t amount, const T& value) {
        allocate_amount_nodes(amount);
        construct_value_list(value);
    }

    List(const Allocator& allocator2) : allocator(allocator2) {
        allocate_amount_nodes(0);
    }

    List(size_t amount, const Allocator& allocator2) : allocator(allocator2) {
        allocate_amount_nodes(amount);
        construct_default_list();
    }

    List(size_t amount, const T& value, const Allocator& allocator2) : allocator(allocator2) {
        allocate_amount_nodes(amount);
        construct_value_list(value);
    }

    List(const List& list) : allocator(AllocTraits::select_on_container_copy_construction(list.allocator)) {
        sz = list.size();
        allocate_amount_nodes(sz);
        Node* it = static_cast<Node*>(fake->next);
        Node* it2 = static_cast<Node*>(list.fake->next);
        while (it != fake) {
            try {
                NodeAllocTraits::construct(node_allocator, it, it->prev, it->next, it2->value);
                it = static_cast<Node*>(it->next);
                it2 = static_cast<Node*>(it2->next);
            } catch (...) {
                return_to_normal(it);
            }
        }
    }

    List& operator=(const List& list) {
        List copy = list;
        swap_without_allocator(copy);
        if (AllocTraits::propagate_on_container_copy_assignment::value) {
            allocator = list.allocator;
        }
        return *this;

    };

    Allocator get_allocator() {
        return allocator;
    }

    size_t size() const {
        return sz;
    }

    template<bool is_const>
    class Iterator {
    private:
        using node_type = std::conditional_t<is_const, const BaseNode*, BaseNode*>;
        using Node_type = std::conditional_t<is_const, const Node*, Node*>;
        node_type ptr;

    public:

        node_type get_ptr() const {
            return ptr;
        }

        using difference_type = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using pointer = std::conditional_t<is_const, const T*, T*>;
        using reference = std::conditional_t<is_const, const T&, T&>;
        using value_type = std::conditional_t<is_const, const T, T>;

        Iterator() = default;

        ~Iterator() = default;

        Iterator(node_type base_node) : ptr(base_node) {}

        Iterator(const Iterator<false>& iterator) : ptr(iterator.get_ptr()) {

        }

        Iterator& operator=(const Iterator& iterator) {
            ptr = iterator.get_ptr();
            return *this;
        }

        reference operator*() {
            return static_cast<Node_type>(ptr)->value;
        };

        pointer operator->() {
            return &(static_cast<Node_type>(ptr)->value);
        }

        Iterator& operator++() {
            ptr = ptr->next;
            return *this;
        }

        Iterator operator++(int) {
            Iterator copy = *this;
            ptr = ptr->next;
            return copy;
        }

        Iterator& operator--() {
            ptr = ptr->prev;
            return *this;
        }

        Iterator operator--(int) {
            Iterator copy = *this;
            ptr = ptr->prev;
            return copy;
        }

        bool operator!=(const Iterator& it) const {
            return ptr != it.get_ptr();
        }

        bool operator==(const Iterator& it) const {
            return ptr == it.get_ptr();
        }

    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    iterator begin() const {
        return iterator((fake->next));
    }

    const_iterator cbegin() const {
        return const_iterator(fake->next);
    }

    iterator end() const {
        return iterator((fake));
    }

    const_iterator cend() const {
        return const_iterator((fake));
    }

    reverse_iterator rbegin() const {
        return reverse_iterator((fake));
    }

    reverse_iterator rend() const {
        return reverse_iterator((fake->next));
    }

    const_reverse_iterator crbegin() const {
        return const_reverse_iterator((fake));
    }

    const_reverse_iterator crend() const {
        return const_reverse_iterator((fake->next));
    }

    iterator insert(const_iterator it, const T& val) {
        Node* new_vertex = NodeAllocTraits::allocate(node_allocator, 1);
        NodeAllocTraits::construct(node_allocator, new_vertex, const_cast<BaseNode*>(it.get_ptr()->prev),
                                   const_cast<BaseNode*>(it.get_ptr()), val);
        const_cast<BaseNode*>(it.get_ptr()->prev)->next = new_vertex;
        const_cast<BaseNode*>(it.get_ptr())->prev = new_vertex;
        sz++;

        return iterator(new_vertex);
    }

    iterator erase(const_iterator it) {
        auto ans = it.get_ptr()->next;
        auto prev = it.get_ptr()->prev;
        const_cast<BaseNode*>(prev)->next = ans;
        const_cast<BaseNode*>(ans)->prev = it.get_ptr()->prev;
        sz--;
        NodeAllocTraits::destroy(node_allocator, static_cast<Node*>(const_cast<BaseNode*>(it.get_ptr())));
        NodeAllocTraits::deallocate(node_allocator, static_cast<Node*>(const_cast<BaseNode*>(it.get_ptr())), 1);
        return ans;
    }

    void push_front(const T& value) {
        insert(const_iterator(static_cast<Node*>(fake->next)), value);
    }

    void push_back(const T& value) {
        Node* new_vertex = NodeAllocTraits::allocate(node_allocator, 1);
        NodeAllocTraits::construct(node_allocator, new_vertex, fake->prev, fake, value);
        fake->prev->next = new_vertex;
        fake->prev = new_vertex;
        sz++;
    }

    void pop_front() {
        erase(const_iterator(fake->next));
    }

    void pop_back() {
        erase(const_iterator(fake->prev));
    }
};

