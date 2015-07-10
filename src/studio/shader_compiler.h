#pragma once


#include <qdockwidget.h>
#include <qmap.h>


namespace Lumix
{
	class WorldEditor;
}


class FileSystemWatcher;


class ShaderCompiler
{
	public:
		ShaderCompiler();
		~ShaderCompiler();

		void setWorldEditor(Lumix::WorldEditor& editor) { m_editor = &editor; }
		void compileAll();

	private:
		void onFileChanged(const char* path);
		void parseDependencies();
		void compile(const QString& path);
		void makeUpToDate();

	private:
		int m_to_compile;
		bool m_is_compiling;
		Lumix::WorldEditor* m_editor;
		FileSystemWatcher* m_watcher;
		QMap<QString, QVector<QString> > m_dependencies;
};
