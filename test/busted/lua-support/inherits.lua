return function( baseClass )
    local new_class = {}
    local class_mt = { __index = new_class }

    function new_class:create()
        local newinst = {}
        setmetatable( newinst, class_mt )
        return newinst
    end

    if baseClass then
        setmetatable( new_class, { __index = baseClass } )
    end

    return new_class
end
