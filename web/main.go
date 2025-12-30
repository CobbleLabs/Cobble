package main

import (
	"embed"
	"html/template"
	"log"
	"net/http"
)

//go:embed templates/* static/*
var content embed.FS

var templates *template.Template

func init() {
	templates = template.Must(template.ParseFS(content, "templates/*.html"))
}

func main() {
	// Static files
	http.Handle("/static/", http.FileServer(http.FS(content)))

	// Routes
	http.HandleFunc("/", homeHandler)
	http.HandleFunc("/docs", docsHandler)
	http.HandleFunc("/docs/", docsHandler)

	log.Println("🚀 Cobble Web running on http://localhost:5050")
	log.Fatal(http.ListenAndServe(":5050", nil))
}

func homeHandler(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	templates.ExecuteTemplate(w, "index.html", nil)
}

func docsHandler(w http.ResponseWriter, r *http.Request) {
	templates.ExecuteTemplate(w, "docs.html", nil)
}
