#ifndef CTILESTAMP_H
#define CTILESTAMP_H

#include <stdint.h>

#include "cprojectbase.h"
#include "tilestampeditorform.h"
#include "ichrrombankitem.h"

#include <QMessageBox>
#include <QTabWidget>

class CTileStamp : public CProjectBase, public IChrRomBankItem
{
   Q_OBJECT
public:
   CTileStamp(IProjectTreeViewItem* parent);
   virtual ~CTileStamp();

   // Member getters
   QByteArray getTileData();
   QByteArray getAttributeData();
   void getSize(int* xSize,int* ySize) { (*xSize) = m_xSize; (*ySize) = m_ySize; }
   QString getAttrTbl() { return m_attrTblUUID; }
   QImage getTileImage();

   TileStampEditorForm* editor() { return dynamic_cast<TileStampEditorForm*>(m_editor); }

   // IXMLSerializable Interface Implementation
   virtual bool serialize(QDomDocument& doc, QDomNode& node);
   virtual bool deserialize(QDomDocument& doc, QDomNode& node, QString& errors);

   // IProjectTreeViewItem Interface Implmentation
   virtual void contextMenuEvent(QContextMenuEvent* event, QTreeView* parent);
   virtual void openItemEvent(CProjectTabWidget* parent);
   virtual void saveItemEvent();
   virtual bool canChangeName();
   virtual bool onNameChanged(QString newName);

   // IChrRomBankItem Interface Implementation
   virtual int getChrRomBankItemSize();
   virtual QByteArray getChrRomBankItemData();
   virtual QIcon getChrRomBankItemIcon();
   virtual QImage getChrRomBankItemImage();
   virtual QString getItemType()
   {
      return "Tile";
   }

private:
   QByteArray m_tile;
   QByteArray m_attr;
   int        m_xSize;
   int        m_ySize;
   QString    m_attrTblUUID;
   bool       m_grid;
};

#endif // CTILESTAMP_H