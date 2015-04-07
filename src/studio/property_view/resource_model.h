#pragma once


#include "core/resource.h"
#include "dynamic_object_model.h"


namespace Lumix
{
	class Material;
	class Resource;
	class WorldEditor;
}


class ResourceModel : public DynamicObjectModel
{
	public:
		explicit ResourceModel(Lumix::WorldEditor& editor, Lumix::Resource* resource);
		~ResourceModel();

	private:
		void onResourceLoaded(Lumix::Resource::State, Lumix::Resource::State new_state);
		void fillModelInfo();
		void fillMaterialInfo();
		void fillTextureInfo();
		void saveMaterial(Lumix::Material* material);
		void showFileDialog(DynamicObjectModel::Node* node, QString filter);
		void setMaterialShader(Lumix::Material* material, QString value);

	private:
		Lumix::Resource* m_resource;
		Lumix::WorldEditor& m_editor;
};
