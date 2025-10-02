Dim xlApp
Dim xlBook

Set xlApp = CreateObject("Excel.Application")
Set xlBook = xlApp.Workbooks.Open("C:\Users\user\Documents\GitHub\Procademy_Assignment\RPCTestServer\RPCSource.xlsm")

xlApp.Run "Main"

xlBook.Close False
xlApp.Quit

Set xlBook = Nothing
Set xlApp = Nothing