#ifndef SUNPARSER_H
#define SUNPARSER_H
// SunParser.h: interface for the CSunParser class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SUNPARSER_H__3B578919_AC8F_438C_81EC_AAC59B692324__INCLUDED_)
#define AFX_SUNPARSER_H__3B578919_AC8F_438C_81EC_AAC59B692324__INCLUDED_

#if _MSC_VER > 1000
/*pragma once removed*/
#endif // _MSC_VER > 1000

//#pragma warning(disable:4786)

#include "GlobalAI.h"





class CSunParser
{
public:
	CSunParser(AIClasses* ai);
		virtual ~CSunParser();

	void LoadVirtualFile(string filename);
	void LoadRealFile(string filename);

	/**
		*  @param value pointer to string to store the value in.
		*  @param ... location of value, terminate with NULL.
		*  @return true on success.
		*/
	bool GetValue(string &value, ...);

	/**
		*  Retreive a specific value from the file and returns it, gives an error messagebox if value not found.
		*  @param location location of value in the form "section\\section\\ ... \\name".
		*  @return returns the value on success, undefined on failure.
		*/
	string SGetValueMSG(string location);

	/**
		*  Retreive a specific value from the file and returns it, returns the specified default value if not found.
		*  @param defaultvalue
		*  @param location location of value.
		*  @return returns the value on success, default otherwise.
		*/
	string SGetValueDef(string defaultvalue, string location);


	/**
		*  Retreive a specific value from the file and returns it.
		*  @param value string to store value in.
		*  @param location location of value in the form "section\\section\\ ... \\name".
		*  @return returns true on success, false otherwise and error message in value.
		*/
	bool SGetValue(string &value, string location);
	const map<string, string> GetAllValues(string location);
	vector<string> GetSectionList(string location);
	bool SectionExist(string location);
	void Test();

	template<typename T>
	void ParseArray(string value, T *array, int length)
	{
		stringstream stream;
		stream << value;

		for(int i=0; i<length; i++)
		{
			stream >> array[i];
			//char slask;
			//stream >> slask;

		}
	}

	//template funktion f�r att h�ta ett v�rde, ger errormessagebox om v�rdet inte fins
	template<typename T>
	void GetMsg(T& value, const string& key)
	{
		string str;
		str = SGetValueMSG(key);

		stringstream stream;
		stream << str;
		stream >> value;
	}

	//template funktion f�r att h�ta ett v�rde, tar defaultv�rde om v�rdet inte fins
	template<typename T>
	void GetDef(T& value, const string& defvalue, const string& key)
	{
//		//L("CSunParser::GetDef(" << (T)value << ", " << defvalue << ", " << key << ")" << endl);
		string str;
		str = SGetValueDef(defvalue, key);

		stringstream stream;
		stream << str;
		stream >> value;
	}

private:
	struct SSection
	{
		map<string, SSection*> sections;
		map<string, string> values;
	};

	map<string,SSection*> sections;

	string filename;

	void Parse(char *buf, int size);
	void DeleteSection(map<string,SSection*> *section);
	vector<string> GetLocationVector(string location);
	char *ParseSection(char *buf, int size, SSection *section);
public:
	float3 GetFloat3(float3 def, string location);
	void LoadBuffer(char* buf, int size);
	AIClasses* ai;
};
#endif // !defined(AFX_SUNPARSER_H__3B578919_AC8F_438C_81EC_AAC59B692324__INCLUDED_)

#endif /* SUNPARSER_H */
