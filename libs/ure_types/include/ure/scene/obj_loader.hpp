#pragma once

#include "mesh.hpp"
#include <string>
#include <memory>
#include <vector>

namespace ure::scene {

class ObjLoader {
public:
    /**
     * @brief 加载 OBJ 文件
     * @param filename 文件路径
     * @return 加载的 Mesh 对象列表 (一个 OBJ 可能包含多个对象，这里简化为合并成一个或返回多个)
     * 目前简化：一个 OBJ 文件加载为一个 Mesh
     */
    static std::shared_ptr<Mesh> load(const std::string& filename);
};

} // namespace ure::scene
