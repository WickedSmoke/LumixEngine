#pragma once


#include "core/resource.h"
#include "dynamic_object_model.h"


namespace Lumix
{
	class Material;
	class Resource;
	class Texture;
	class WorldEditor;
}


class ResourceModel : public DynamicObjectModel
{
	public:
		ResourceModel(Lumix::WorldEditor& editor, const Lumix::Path& path);
		~ResourceModel();

		Lumix::Resource* getResource() { return m_resource; }
		void setResource(const Lumix::Path& path);

	private:
		void onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state);
		void fillModelInfo();
		void fillMaterialInfo(Lumix::Material* material, Node& node);
		void fillTextureInfo(Lumix::Texture*, Node& node);
		void saveMaterial(Lumix::Material* material);
		void showFileDialog(DynamicObjectModel::Node* node, QString filter);
		void setMaterialShader(Lumix::Material* material, QString value);

	private:
		Lumix::Resource* m_resource;
		Lumix::WorldEditor& m_editor;
		uint32_t m_resource_type;
};
